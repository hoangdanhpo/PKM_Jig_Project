import tkinter as tk
from tkinter import ttk
import serial.tools.list_ports as list_ports
import serial
import tkinter.messagebox as messagebox

connected = False
ser = None
rx_buffer = b''

def refresh_ports():
    port_options = []
    ports = list_ports.comports()
    for p in ports:
        port_options.append(p.device)
    port_dropdown.config(values=port_options)

def toggle_options():
    if connected:
        option1_radio.config(state="normal")
        option2_radio.config(state="normal")
        option_radio2_1.config(state="normal")
        option_radio2_2.config(state="normal")
        option_radio2_3.config(state="normal")
        option_radio2_4.config(state="normal")
    else:
        option1_radio.config(state="disabled")
        option2_radio.config(state="disabled")
        option_radio2_1.config(state="disabled")
        option_radio2_2.config(state="disabled")
        option_radio2_3.config(state="disabled")
        option_radio2_4.config(state="disabled")

def f_0V_selected():
    if connected:
        ser.write(b'1')
def f_12V_selected():
    if connected:
        ser.write(b'2')
def f_24V_selected():
    if connected:
        ser.write(b'3')
def f_ST_uC_selected():
    if connected:
        ser.write(b'4')
def f_BLE_selected():
    if connected:
        ser.write(b'5')
def f_UWB_selected():
    if connected:
        ser.write(b'6')
def f_Not_Use_selected():
    if connected:
        ser.write(b'7')

# Read "V:12.34,I:1.23\r\n" frames from JIG and update tracking labels
def poll_tracking():
    global rx_buffer
    if not connected:
        return
    try:
        if ser.in_waiting:
            rx_buffer += ser.read(ser.in_waiting)
    except serial.SerialException:
        return
    while b'\n' in rx_buffer:
        line, rx_buffer = rx_buffer.split(b'\n', 1)
        try:
            fields = dict(item.split(':') for item in line.decode().strip().split(','))
            voltage_var.set(fields['V'] + " V")
            current_var.set(fields['I'] + " A")
        except (ValueError, KeyError, UnicodeDecodeError):
            pass
    root.after(100, poll_tracking)

def connect():
    global connected, ser, rx_buffer
    if not connected:
        port = port_dropdown.get()
        ser = serial.Serial(port)
        if ser.is_open:
            connected = True
            connect_button.config(text="Disconnect")
            messagebox.showinfo("Connect", "Connected successfully!")
            ser.reset_input_buffer()
            rx_buffer = b''
            ser.write(b'c')
            toggle_options()
            poll_tracking()
        else:
            messagebox.showerror("Connect", "Connection failed!")
    else:
        connected = False
        connect_button.config(text="Connect")
        messagebox.showinfo("Disconnect", "Disconnected successfully!")
        ser.write(b'd')
        ser.close()
        option_var.set(None)
        option_var2.set(None)
        voltage_var.set("--")
        current_var.set("--")
        toggle_options()



root = tk.Tk()
root.title("PKM_JIG Project")

port_frame = ttk.LabelFrame(root, text="Serial Port")
port_frame.grid(row=0, column=0, padx=10, pady=10, sticky="nsew")

port_label = ttk.Label(port_frame, text="Select Port:")
port_label.grid(row=0, column=0, padx=5, pady=5)

port_dropdown = ttk.Combobox(port_frame, state="readonly")
port_dropdown.grid(row=1, column=0, padx=5, pady=5)

refresh_button = ttk.Button(port_frame, text="Refresh", command=refresh_ports)
refresh_button.grid(row=1, column=1, padx=5, pady=5)

connect_button = ttk.Button(port_frame, text="Connect", command=connect)
connect_button.grid(row=2, column=0, columnspan=2, padx=5, pady=5)

option_frame = ttk.LabelFrame(root, text="Power Control")
option_frame.grid(row=0, column=1, padx=10, pady=10, sticky="nsew")

option_var = tk.StringVar()

option1_radio = ttk.Radiobutton(option_frame, text="Disable", variable=option_var, value="Disable", state="disabled", command = f_0V_selected)
option1_radio.grid(row=0, column=0, padx=10, pady=5, sticky="w")

option2_radio = ttk.Radiobutton(option_frame, text="Enable", variable=option_var, value="Enable", state="disabled", command = f_12V_selected)
option2_radio.grid(row=1, column=0, padx=10, pady=5, sticky="w")

option_frame2 = ttk.LabelFrame(root, text="Switch signal")
option_frame2.grid(row=0, column=3, padx=10, pady=10, sticky="nsew")

option_var2 = tk.StringVar()

option_radio2_1 = ttk.Radiobutton(option_frame2, text="ST uC", variable=option_var2, value="ST uC", state="disabled", command = f_ST_uC_selected)
option_radio2_1.grid(row=0, column=0, padx=10, pady=5, sticky="w")
option_radio2_2 = ttk.Radiobutton(option_frame2, text="BLE", variable=option_var2, value="BLE", state="disabled", command = f_BLE_selected)
option_radio2_2.grid(row=1, column=0, padx=10, pady=5, sticky="w")
option_radio2_3 = ttk.Radiobutton(option_frame2, text="UWB", variable=option_var2, value="UWB", state="disabled", command = f_UWB_selected)
option_radio2_3.grid(row=2, column=0, padx=10, pady=5, sticky="w")
option_radio2_4 = ttk.Radiobutton(option_frame2, text="Not use", variable=option_var2, value="Not use", state="disabled", command = f_Not_Use_selected)
option_radio2_4.grid(row=3, column=0, padx=10, pady=5, sticky="w")

tracking_frame = ttk.LabelFrame(root, text="Status")
tracking_frame.grid(row=0, column=4, padx=10, pady=10, sticky="nsew")

voltage_var = tk.StringVar(value="--")
current_var = tk.StringVar(value="--")

ttk.Label(tracking_frame, text="Voltage:").grid(row=0, column=0, padx=10, pady=5, sticky="w")
ttk.Label(tracking_frame, textvariable=voltage_var, width=8).grid(row=0, column=1, padx=10, pady=5, sticky="w")
ttk.Label(tracking_frame, text="Current:").grid(row=1, column=0, padx=10, pady=5, sticky="w")
ttk.Label(tracking_frame, textvariable=current_var, width=8).grid(row=1, column=1, padx=10, pady=5, sticky="w")

def on_close():
    if connected == True:
        ser.write(b'd')
        ser.close()
    root.destroy()

root.protocol("WM_DELETE_WINDOW", on_close)

refresh_ports()

root.mainloop()
