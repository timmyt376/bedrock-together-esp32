
import socket

HOST = "ESP32_IP"
PORT = 7777

s = socket.socket()
s.connect((HOST, PORT))
print("Connected. Use AUTH <key>")

while True:
    cmd = input("> ")
    s.sendall((cmd + "\n").encode())
    print(s.recv(4096).decode())
