#!/usr/bin/env python3
import serial
import time
import csv
import os
import glob
from datetime import datetime
import argparse

CSV_HEADERS = ["Timestamp", "Event", "MessageNumber", "Distance(m)", "Received", "Lost", "RSSI", "SNR", "TotalPackets", "LossRate"]

def find_serial_port():
    acm_ports = glob.glob('/dev/ttyACM*')
    if acm_ports:
        print("Found ACM ports:", ", ".join(acm_ports))
        return acm_ports[0]
    usb_ports = glob.glob('/dev/ttyUSB*')
    if usb_ports:
        print("Found USB ports:", ", ".join(usb_ports))
        return usb_ports[0]
    print("No serial ports found. Retrying...")
    return None

def continuously_check_for_port(timeout=60):
    start_time = time.time()
    while time.time() - start_time < timeout:
        port = find_serial_port()
        if port:
            return port
        time.sleep(5)
    return None

def process_data_line(line):
    # Expected CSV format from the receiver:
    # DATA,[boardTimestamp],[MessageNumber],[Distance(m)],[Received],[Lost],[RSSI],[SNR],[TotalPackets],[LossRate]
    parts = line.split(",")
    if len(parts) < 10:
        print("Incomplete DATA line received:", line)
        return None
    try:
        data = {
            "Timestamp": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
            "Event": "DATA",
            "MessageNumber": parts[2],
            "Distance(m)": parts[3],
            "Received": parts[4],
            "Lost": parts[5],
            "RSSI": parts[6],
            "SNR": parts[7],
            "TotalPackets": parts[8],
            "LossRate": parts[9].strip()  # remove newline if any
        }
        # If TotalPackets is "0", use Received value instead.
        if data["TotalPackets"].strip() == "0":
            data["TotalPackets"] = data["Received"]
        return data
    except Exception as e:
        print("Error processing DATA line:", e)
        return None

def process_lost_line(line):
    # Expected format: "LOST: Packet <number>"
    # We'll extract the lost packet number and output a row indicating a lost event.
    try:
        parts = line.split()
        # Parts example: ["LOST:", "Packet", "9"]
        lost_packet = parts[-1]
        data = {
            "Timestamp": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
            "Event": "LOST",
            "MessageNumber": lost_packet,  # mark which packet was lost
            "Distance(m)": "",   # you might fill these later if needed
            "Received": "",
            "Lost": "",
            "RSSI": "",
            "SNR": "",
            "TotalPackets": "",
            "LossRate": ""
        }
        return data
    except Exception as e:
        print("Error processing LOST line:", e)
        return None

def log_data(data, csv_log):
    # Log the data to CSV. We'll open the file in append mode and write headers if the file doesn't exist.
    file_exists = os.path.isfile(csv_log)
    try:
        with open(csv_log, "a", newline="") as csvfile:
            writer = csv.DictWriter(csvfile, fieldnames=CSV_HEADERS)
            if not file_exists:
                writer.writeheader()
            writer.writerow(data)
        print("Logged data:", data)
    except Exception as e:
        print("Error writing to CSV:", e)

def main():
    parser = argparse.ArgumentParser(description='LoRa Data Logger for DomH-giti')
    parser.add_argument('--port', help='Serial port (e.g. /dev/ttyACM0)')
    parser.add_argument('--baud', type=int, default=115200, help='Baud rate')
    parser.add_argument('--log-dir', default=os.path.expanduser("~/lora_data_logs"), help='Directory for logs')
    parser.add_argument('--wait-for-port', type=int, default=60, help='Seconds to wait for serial port')
    args = parser.parse_args()
    
    # Setup log directory
    log_dir = args.log_dir
    os.makedirs(log_dir, exist_ok=True)
    
    current_csv_log = None
    last_distance = None     # Will store the last valid float distance
    last_lost = None         # Will store the last valid lost packets count

    print("Using log directory:", log_dir)
    port = args.port
    if not port:
        print("Auto-detecting serial port...")
        port = continuously_check_for_port(args.wait_for_port)
    if not port:
        print("ERROR: Could not find a serial port.")
        return
    
    print("Serial port:", port)
    
    try:
        ser = serial.Serial(port, args.baud, timeout=1)
        # Flush any preexisting data
        ser.reset_input_buffer()
    except Exception as e:
        print("Failed to open serial port:", e)
        return
    
    print("Serial connection established on", port)
    print("Waiting for data... (Press Ctrl+C to stop)")
    
    while True:
        try:
            if ser.in_waiting:
                # Read the next line from the serial port
                line = ser.readline().decode('utf-8', errors='replace').strip()
                if line:
                    print("RAW:", line)
                    
                    # Ignore "Large gap detected" lines; they don't update the Lost count.
                    if line.startswith("Large gap detected"):
                        print("Ignoring large gap message; not updating Lost count.")
                        continue
                    
                    # Process lost packet lines
                    if line.startswith("LOST:"):
                        lost_data = process_lost_line(line)
                        if lost_data and current_csv_log:
                            log_data(lost_data, current_csv_log)
                        else:
                            print("No CSV log file created yet; LOST event not logged to CSV.")
                        continue

                    # Process DATA lines as before
                    if line.startswith("DATA,"):
                        data = process_data_line(line)
                        if data is None:
                            continue
                        try:
                            current_distance = float(data["Distance(m)"])
                        except Exception as e:
                            print("Error converting distance to float:", e)
                            continue
                        
                        # If a transient or malformed distance is detected (0.00 while last valid distance > 0),
                        # continue logging in the current session and override Lost with the last valid value.
                        if last_distance is not None and current_distance == 0.0 and last_distance != 0.0:
                            print(f"Transient 0.00 value detected, using last valid Lost count: {last_lost if last_lost is not None else 0}")
                            data["Lost"] = str(last_lost if last_lost is not None else 0)
                        else:
                            # Valid distance reading; update last_lost and last_distance if needed.
                            try:
                                current_lost = int(float(data["Lost"]))
                            except Exception:
                                current_lost = 0
                            last_lost = current_lost
                        
                        # Create a new log file only if:
                        # 1) No file exists yet or
                        # 2) A new session is detected with a higher (and valid) distance.
                        if last_distance is None:
                            last_distance = current_distance
                            timestamp = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
                            current_csv_log = os.path.join(log_dir, f"lora_data_{current_distance:.2f}m_{timestamp}.csv")
                            print(f"New session detected: New distance ({current_distance:.2f} m). Creating log file: {current_csv_log}")
                        elif current_distance <= last_distance and current_distance != last_distance:
                            print(f"Transient or lower value detected (Current: {current_distance:.2f} m, Last: {last_distance:.2f} m). Continuing to log data in file for {last_distance:.2f} m")
                        elif current_distance > last_distance:
                            last_distance = current_distance
                            timestamp = datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
                            current_csv_log = os.path.join(log_dir, f"lora_data_{current_distance:.2f}m_{timestamp}.csv")
                            print(f"New distance detected ({current_distance:.2f} m). Creating new log file: {current_csv_log}")
                            
                        if current_csv_log:
                            log_data(data, current_csv_log)
            else:
                time.sleep(0.1)
        except KeyboardInterrupt:
            print("Exiting...")
            break
        except Exception as e:
            print("Error reading from serial:", e)
            time.sleep(1)

if __name__ == "__main__":
    main()