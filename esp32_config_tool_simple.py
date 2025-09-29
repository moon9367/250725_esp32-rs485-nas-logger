#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ESP32 RS485 Modbus Configuration Tool (USB Serial)
WiFi password change and environment setup via COM port
"""

import tkinter as tk
from tkinter import ttk, messagebox, scrolledtext
import serial
import serial.tools.list_ports
import threading
import time
import json
import re
from typing import Dict, List, Optional

class ESP32ConfigTool:
    def __init__(self):
        self.root = tk.Tk()
        self.root.title("ESP32 RS485 Modbus Configuration Tool (USB)")
        self.root.geometry("800x900")
        self.root.configure(bg='#f0f0f0')
        
        # Serial connection info
        self.com_port = tk.StringVar(value="")
        self.baudrate = tk.IntVar(value=115200)
        self.serial_conn = None
        self.connected = False
        
        # Configuration variables (same as web interface)
        self.wifi_ssid = tk.StringVar(value="TSPOL")
        self.wifi_password = tk.StringVar(value="mms56529983")
        self.nas_url = tk.StringVar(value="http://tspol.iptime.org:8888/rs485/upload.php")
        
        # Modbus settings
        self.target_addresses = tk.StringVar(value="203,212,218,227,230,233")
        self.zero_based = tk.BooleanVar(value=False)
        self.data_type = tk.StringVar(value="FLOAT")
        self.registers_per_value = tk.IntVar(value=2)
        self.byte_swap = tk.BooleanVar(value=False)
        self.word_swap = tk.BooleanVar(value=False)
        self.continuous_read = tk.BooleanVar(value=False)
        
        # Timing settings
        self.collection_interval = tk.IntVar(value=300)
        self.transmission_interval = tk.IntVar(value=1800)
        self.time_mode = tk.BooleanVar(value=False)  # False=relative, True=absolute
        
        # Initialize log_text first
        self.log_text = None
        self.create_widgets()
        
    def create_widgets(self):
        # Main frame
        main_frame = ttk.Frame(self.root, padding="10")
        main_frame.grid(row=0, column=0, sticky=(tk.W, tk.E, tk.N, tk.S))
        
        # COM port connection section
        self.create_connection_section(main_frame, 0)
        
        # Network settings
        self.create_network_section(main_frame, 1)
        
        # Modbus settings
        self.create_modbus_section(main_frame, 2)
        
        # Timing settings
        self.create_timing_section(main_frame, 3)
        
        # Buttons
        self.create_buttons(main_frame, 4)
        
        # Log area
        self.create_log_section(main_frame, 5)
        
    def create_connection_section(self, parent, row):
        # COM port connection settings
        conn_frame = ttk.LabelFrame(parent, text="COM Port Connection", padding="10")
        conn_frame.grid(row=row, column=0, columnspan=2, sticky=(tk.W, tk.E), pady=5)
        
        # COM port selection
        ttk.Label(conn_frame, text="COM Port:").grid(row=0, column=0, sticky=tk.W, padx=(0, 10))
        self.com_port_combo = ttk.Combobox(conn_frame, textvariable=self.com_port, width=15, state="readonly")
        self.com_port_combo.grid(row=0, column=1, sticky=tk.W)
        ttk.Button(conn_frame, text="Refresh Ports", command=self.refresh_ports).grid(row=0, column=2, padx=(10, 0))
        
        # Baudrate
        ttk.Label(conn_frame, text="Baudrate:").grid(row=0, column=3, sticky=tk.W, padx=(20, 10))
        ttk.Entry(conn_frame, textvariable=self.baudrate, width=10).grid(row=0, column=4, sticky=tk.W)
        
        # Connect/Disconnect button
        self.connect_btn = ttk.Button(conn_frame, text="Connect", command=self.toggle_connection)
        self.connect_btn.grid(row=0, column=5, padx=(20, 0))
        
        # Initial port refresh
        self.refresh_ports()
        
    def create_network_section(self, parent, row):
        # Network settings
        net_frame = ttk.LabelFrame(parent, text="Network Settings (WiFi Change)", padding="10")
        net_frame.grid(row=row, column=0, columnspan=2, sticky=(tk.W, tk.E), pady=5)
        
        ttk.Label(net_frame, text="WiFi SSID:").grid(row=0, column=0, sticky=tk.W, pady=2)
        ttk.Entry(net_frame, textvariable=self.wifi_ssid, width=30).grid(row=0, column=1, sticky=tk.W, padx=(10, 0))
        
        ttk.Label(net_frame, text="WiFi Password:").grid(row=1, column=0, sticky=tk.W, pady=2)
        ttk.Entry(net_frame, textvariable=self.wifi_password, width=30, show="*").grid(row=1, column=1, sticky=tk.W, padx=(10, 0))
        
        ttk.Label(net_frame, text="NAS URL:").grid(row=2, column=0, sticky=tk.W, pady=2)
        ttk.Entry(net_frame, textvariable=self.nas_url, width=50).grid(row=2, column=1, sticky=tk.W, padx=(10, 0))
        
    def create_modbus_section(self, parent, row):
        # Modbus settings
        modbus_frame = ttk.LabelFrame(parent, text="Modbus Settings", padding="10")
        modbus_frame.grid(row=row, column=0, columnspan=2, sticky=(tk.W, tk.E), pady=5)
        
        # Target addresses
        ttk.Label(modbus_frame, text="Target Addresses:").grid(row=0, column=0, sticky=tk.W, pady=2)
        ttk.Entry(modbus_frame, textvariable=self.target_addresses, width=30).grid(row=0, column=1, sticky=tk.W, padx=(10, 0))
        ttk.Label(modbus_frame, text="(comma separated, e.g.: 203,212,218,227)", font=("Arial", 8)).grid(row=0, column=2, sticky=tk.W, padx=(10, 0))
        
        # Address mode
        ttk.Label(modbus_frame, text="Address Mode:").grid(row=1, column=0, sticky=tk.W, pady=2)
        addr_frame = ttk.Frame(modbus_frame)
        addr_frame.grid(row=1, column=1, sticky=tk.W, padx=(10, 0))
        ttk.Radiobutton(addr_frame, text="0-based address (vendor spec)", variable=self.zero_based, value=False).pack(anchor=tk.W)
        ttk.Radiobutton(addr_frame, text="1-based address (auto -1)", variable=self.zero_based, value=True).pack(anchor=tk.W)
        
        # Data type
        ttk.Label(modbus_frame, text="Data Type:").grid(row=2, column=0, sticky=tk.W, pady=2)
        data_type_combo = ttk.Combobox(modbus_frame, textvariable=self.data_type, width=15, state="readonly")
        data_type_combo['values'] = ("INT16", "UINT16", "INT32", "FLOAT")
        data_type_combo.grid(row=2, column=1, sticky=tk.W, padx=(10, 0))
        
        # Registers per value
        ttk.Label(modbus_frame, text="Registers/Value:").grid(row=3, column=0, sticky=tk.W, pady=2)
        reg_combo = ttk.Combobox(modbus_frame, textvariable=self.registers_per_value, width=15, state="readonly")
        reg_combo['values'] = (1, 2, 3, 4)
        reg_combo.grid(row=3, column=1, sticky=tk.W, padx=(10, 0))
        ttk.Label(modbus_frame, text="Select according to data type", font=("Arial", 8)).grid(row=3, column=2, sticky=tk.W, padx=(10, 0))
        
        # Byte order
        ttk.Label(modbus_frame, text="Byte Order:").grid(row=4, column=0, sticky=tk.W, pady=2)
        byte_frame = ttk.Frame(modbus_frame)
        byte_frame.grid(row=4, column=1, sticky=tk.W, padx=(10, 0))
        ttk.Radiobutton(byte_frame, text="Big Endian (default)", variable=self.byte_swap, value=False).pack(anchor=tk.W)
        ttk.Radiobutton(byte_frame, text="Little Endian", variable=self.byte_swap, value=True).pack(anchor=tk.W)
        
        # Word order
        ttk.Label(modbus_frame, text="Word Order:").grid(row=5, column=0, sticky=tk.W, pady=2)
        word_frame = ttk.Frame(modbus_frame)
        word_frame.grid(row=5, column=1, sticky=tk.W, padx=(10, 0))
        ttk.Radiobutton(word_frame, text="High Word First (default)", variable=self.word_swap, value=False).pack(anchor=tk.W)
        ttk.Radiobutton(word_frame, text="Low Word First", variable=self.word_swap, value=True).pack(anchor=tk.W)
        
        # Read mode
        ttk.Label(modbus_frame, text="Read Mode:").grid(row=6, column=0, sticky=tk.W, pady=2)
        read_frame = ttk.Frame(modbus_frame)
        read_frame.grid(row=6, column=1, sticky=tk.W, padx=(10, 0))
        ttk.Radiobutton(read_frame, text="Continuous read (fast, weather sensor)", variable=self.continuous_read, value=True).pack(anchor=tk.W)
        ttk.Radiobutton(read_frame, text="Individual read (other device compatible)", variable=self.continuous_read, value=False).pack(anchor=tk.W)
        
    def create_timing_section(self, parent, row):
        # Timing settings
        timing_frame = ttk.LabelFrame(parent, text="Timing Settings", padding="10")
        timing_frame.grid(row=row, column=0, columnspan=2, sticky=(tk.W, tk.E), pady=5)
        
        # Data collection interval
        ttk.Label(timing_frame, text="Collection Interval (sec):").grid(row=0, column=0, sticky=tk.W, pady=2)
        ttk.Spinbox(timing_frame, from_=5, to=3600, textvariable=self.collection_interval, width=10).grid(row=0, column=1, sticky=tk.W, padx=(10, 0))
        ttk.Label(timing_frame, text="Min 5 sec (ESP32 memory protection)", font=("Arial", 8)).grid(row=0, column=2, sticky=tk.W, padx=(10, 0))
        
        # Data transmission interval
        ttk.Label(timing_frame, text="Transmission Interval (sec):").grid(row=1, column=0, sticky=tk.W, pady=2)
        ttk.Spinbox(timing_frame, from_=10, to=86400, textvariable=self.transmission_interval, width=10).grid(row=1, column=1, sticky=tk.W, padx=(10, 0))
        
        # Time mode
        ttk.Label(timing_frame, text="Time Mode:").grid(row=2, column=0, sticky=tk.W, pady=2)
        time_frame = ttk.Frame(timing_frame)
        time_frame.grid(row=2, column=1, sticky=tk.W, padx=(10, 0))
        ttk.Radiobutton(time_frame, text="Relative time (collection start)", variable=self.time_mode, value=False).pack(anchor=tk.W)
        ttk.Radiobutton(time_frame, text="Absolute time (real time)", variable=self.time_mode, value=True).pack(anchor=tk.W)
        
    def create_buttons(self, parent, row):
        # Button frame
        button_frame = ttk.Frame(parent)
        button_frame.grid(row=row, column=0, columnspan=2, pady=20)
        
        ttk.Button(button_frame, text="Read Settings", command=self.read_current_settings).pack(side=tk.LEFT, padx=5)
        ttk.Button(button_frame, text="Save Settings", command=self.save_settings).pack(side=tk.LEFT, padx=5)
        ttk.Button(button_frame, text="Load Settings", command=self.load_settings).pack(side=tk.LEFT, padx=5)
        ttk.Button(button_frame, text="Reset Settings", command=self.reset_settings).pack(side=tk.LEFT, padx=5)
        ttk.Button(button_frame, text="Reboot ESP32", command=self.reboot_esp32).pack(side=tk.LEFT, padx=5)
        
    def create_log_section(self, parent, row):
        # Log area
        log_frame = ttk.LabelFrame(parent, text="Serial Communication Log", padding="10")
        log_frame.grid(row=row, column=0, columnspan=2, sticky=(tk.W, tk.E, tk.N, tk.S), pady=5)
        
        self.log_text = scrolledtext.ScrolledText(log_frame, height=15, width=80)
        self.log_text.pack(fill=tk.BOTH, expand=True)
        
    def refresh_ports(self):
        """Refresh COM port list"""
        ports = serial.tools.list_ports.comports()
        port_list = [port.device for port in ports]
        self.com_port_combo['values'] = port_list
        if port_list and not self.com_port.get():
            self.com_port.set(port_list[0])
        self.log(f"Available COM ports: {', '.join(port_list) if port_list else 'None'}")
        
    def log(self, message):
        """Log message output"""
        if self.log_text is not None:
            timestamp = time.strftime("%H:%M:%S")
            self.log_text.insert(tk.END, f"[{timestamp}] {message}\n")
            self.log_text.see(tk.END)
            self.root.update_idletasks()
        
    def toggle_connection(self):
        """COM port connect/disconnect"""
        if not self.connected:
            self.connect_serial()
        else:
            self.disconnect_serial()
            
    def connect_serial(self):
        """Connect serial port"""
        try:
            if not self.com_port.get():
                messagebox.showerror("Error", "Please select a COM port.")
                return
                
            self.serial_conn = serial.Serial(
                port=self.com_port.get(),
                baudrate=self.baudrate.get(),
                timeout=2
            )
            
            self.connected = True
            self.connect_btn.config(text="Disconnect")
            self.log(f"SUCCESS: COM port connected: {self.com_port.get()}")
            
            # Wait after connection
            time.sleep(1)
            self.serial_conn.flushInput()
            self.serial_conn.flushOutput()
            
        except Exception as e:
            self.log(f"ERROR: COM port connection failed: {str(e)}")
            messagebox.showerror("Connection Failed", f"COM port connection failed:\n{str(e)}")
            
    def disconnect_serial(self):
        """Disconnect serial port"""
        try:
            if self.serial_conn and self.serial_conn.is_open:
                self.serial_conn.close()
            self.connected = False
            self.connect_btn.config(text="Connect")
            self.log("DISCONNECTED: COM port disconnected")
        except Exception as e:
            self.log(f"ERROR: COM port disconnect error: {str(e)}")
            
    def send_command(self, command: str, timeout: float = 3.0) -> str:
        """Send command to ESP32"""
        if not self.connected or not self.serial_conn:
            raise Exception("Serial port not connected.")
            
        try:
            # Send command
            self.serial_conn.write(f"{command}\n".encode('utf-8'))
            self.serial_conn.flush()
            
            # Read response
            response = ""
            start_time = time.time()
            
            while time.time() - start_time < timeout:
                if self.serial_conn.in_waiting > 0:
                    data = self.serial_conn.read(self.serial_conn.in_waiting)
                    response += data.decode('utf-8', errors='ignore')
                    
                    # Check if response is complete
                    if ">" in response or "ESP32" in response:
                        break
                time.sleep(0.1)
                
            return response.strip()
            
        except Exception as e:
            raise Exception(f"Command send failed: {str(e)}")
            
    def read_current_settings(self):
        """Read current settings"""
        def read_thread():
            try:
                if not self.connected:
                    messagebox.showerror("Error", "Please connect to COM port first.")
                    return
                    
                self.log("Reading current settings from ESP32...")
                
                # Read serial monitor output to get current settings
                time.sleep(1)
                self.serial_conn.flushInput()
                
                # Reboot ESP32 to get setting information
                self.send_command("reset")
                time.sleep(3)
                
                # Read setting information
                response = ""
                start_time = time.time()
                while time.time() - start_time < 10:
                    if self.serial_conn.in_waiting > 0:
                        data = self.serial_conn.read(self.serial_conn.in_waiting)
                        response += data.decode('utf-8', errors='ignore')
                        
                        # Check if all setting information is output
                        if "System ready" in response or "Data collection started" in response:
                            break
                    time.sleep(0.1)
                
                self.log("SUCCESS: ESP32 setting information read complete")
                self.log("Check setting information in serial output")
                
                # Display setting information in log
                self.log_text.insert(tk.END, "\n=== ESP32 Setting Information ===\n")
                self.log_text.insert(tk.END, response)
                self.log_text.insert(tk.END, "\n================================\n")
                self.log_text.see(tk.END)
                
            except Exception as e:
                self.log(f"ERROR: Setting read error: {str(e)}")
                messagebox.showerror("Read Error", f"Error occurred while reading settings:\n{str(e)}")
                
        threading.Thread(target=read_thread, daemon=True).start()
        
    def save_settings(self):
        """Save settings"""
        def save_thread():
            try:
                if not self.connected:
                    messagebox.showerror("Error", "Please connect to COM port first.")
                    return
                    
                self.log("Saving settings to ESP32...")
                
                # Prepare setting data as serial commands
                commands = [
                    f"wifi_ssid:{self.wifi_ssid.get()}",
                    f"wifi_password:{self.wifi_password.get()}",
                    f"nas_url:{self.nas_url.get()}",
                    f"target_addresses:{self.target_addresses.get()}",
                    f"zero_based:{self.zero_based.get()}",
                    f"data_type:{self.data_type.get()}",
                    f"registers_per_value:{self.registers_per_value.get()}",
                    f"byte_swap:{self.byte_swap.get()}",
                    f"word_swap:{self.word_swap.get()}",
                    f"continuous_read:{self.continuous_read.get()}",
                    f"collection_interval:{self.collection_interval.get()}",
                    f"transmission_interval:{self.transmission_interval.get()}",
                    f"time_mode:{self.time_mode.get()}",
                    "save_settings"
                ]
                
                for cmd in commands:
                    response = self.send_command(cmd, 2.0)
                    self.log(f"SEND: {cmd} -> {response[:50]}...")
                    time.sleep(0.5)
                
                self.log("SUCCESS: Settings saved complete")
                messagebox.showinfo("Save Complete", "Settings successfully saved to ESP32.")
                
            except Exception as e:
                self.log(f"ERROR: Setting save error: {str(e)}")
                messagebox.showerror("Save Error", f"Error occurred while saving settings:\n{str(e)}")
                
        threading.Thread(target=save_thread, daemon=True).start()
        
    def load_settings(self):
        """Load settings from local file"""
        try:
            with open('esp32_settings.json', 'r', encoding='utf-8') as f:
                settings = json.load(f)
                
            # Apply setting values
            self.wifi_ssid.set(settings.get('wifi_ssid', 'TSPOL'))
            self.wifi_password.set(settings.get('wifi_password', ''))
            self.nas_url.set(settings.get('nas_url', ''))
            self.target_addresses.set(settings.get('target_addresses', '203,212,218,227'))
            self.zero_based.set(settings.get('zero_based', False))
            self.data_type.set(settings.get('data_type', 'FLOAT'))
            self.registers_per_value.set(settings.get('registers_per_value', 2))
            self.byte_swap.set(settings.get('byte_swap', False))
            self.word_swap.set(settings.get('word_swap', False))
            self.continuous_read.set(settings.get('continuous_read', False))
            self.collection_interval.set(settings.get('collection_interval', 300))
            self.transmission_interval.set(settings.get('transmission_interval', 1800))
            self.time_mode.set(settings.get('time_mode', False))
            
            self.log("SUCCESS: Loaded from local settings file")
            messagebox.showinfo("Load Complete", "Settings loaded from local settings file.")
            
        except FileNotFoundError:
            self.log("WARNING: Settings file not found. Using default values.")
            messagebox.showwarning("File Not Found", "Settings file not found. Using default values.")
        except Exception as e:
            self.log(f"ERROR: Setting load error: {str(e)}")
            messagebox.showerror("Load Error", f"Error occurred while loading settings:\n{str(e)}")
            
    def reset_settings(self):
        """Reset settings"""
        if messagebox.askyesno("Reset Settings", "Reset all settings to default values?"):
            # Reset to default values
            self.wifi_ssid.set("TSPOL")
            self.wifi_password.set("mms56529983")
            self.nas_url.set("http://tspol.iptime.org:8888/rs485/upload.php")
            self.target_addresses.set("203,212,218,227,230,233")
            self.zero_based.set(False)
            self.data_type.set("FLOAT")
            self.registers_per_value.set(2)
            self.byte_swap.set(False)
            self.word_swap.set(False)
            self.continuous_read.set(False)
            self.collection_interval.set(300)
            self.transmission_interval.set(1800)
            self.time_mode.set(False)
            
            self.log("RESET: Settings reset to default values.")
            
    def reboot_esp32(self):
        """Reboot ESP32"""
        def reboot_thread():
            try:
                if not self.connected:
                    messagebox.showerror("Error", "Please connect to COM port first.")
                    return
                    
                self.log("Rebooting ESP32...")
                response = self.send_command("reset", 5.0)
                self.log("SUCCESS: ESP32 reboot complete")
                messagebox.showinfo("Reboot Complete", "ESP32 has been rebooted.")
                
            except Exception as e:
                self.log(f"ERROR: Reboot error: {str(e)}")
                messagebox.showerror("Reboot Error", f"Error occurred while rebooting ESP32:\n{str(e)}")
                
        threading.Thread(target=reboot_thread, daemon=True).start()
        
    def save_local_settings(self):
        """Save to local settings file"""
        try:
            settings = {
                'wifi_ssid': self.wifi_ssid.get(),
                'wifi_password': self.wifi_password.get(),
                'nas_url': self.nas_url.get(),
                'target_addresses': self.target_addresses.get(),
                'zero_based': self.zero_based.get(),
                'data_type': self.data_type.get(),
                'registers_per_value': self.registers_per_value.get(),
                'byte_swap': self.byte_swap.get(),
                'word_swap': self.word_swap.get(),
                'continuous_read': self.continuous_read.get(),
                'collection_interval': self.collection_interval.get(),
                'transmission_interval': self.transmission_interval.get(),
                'time_mode': self.time_mode.get()
            }
            
            with open('esp32_settings.json', 'w', encoding='utf-8') as f:
                json.dump(settings, f, indent=2, ensure_ascii=False)
                
            self.log("SUCCESS: Local settings file saved")
            
        except Exception as e:
            self.log(f"ERROR: Local settings save error: {str(e)}")
            
    def run(self):
        """Run GUI"""
        # Save local settings and disconnect serial on window close
        self.root.protocol("WM_DELETE_WINDOW", self.on_closing)
        
        # Try to load default settings
        try:
            self.load_settings()
        except:
            pass
            
        self.log("STARTED: ESP32 RS485 Modbus Configuration Tool (USB)")
        self.log("INFO: Select COM port and click 'Connect'")
        self.log("INFO: Use this tool to change WiFi password or environment settings")
        
        self.root.mainloop()
        
    def on_closing(self):
        """Called when program closes"""
        if self.connected:
            self.disconnect_serial()
        self.save_local_settings()
        self.root.destroy()

if __name__ == "__main__":
    app = ESP32ConfigTool()
    app.run()
