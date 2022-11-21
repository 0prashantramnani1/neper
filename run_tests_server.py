import sys
import subprocess
import socket
import time

control_plane_port = int(sys.argv[1])

#Creating the socket
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

#Binding and listening to all interfaces
serv_addr = ("0.0.0.0", control_plane_port)
sock.bind(serv_addr)

sock.listen()
#conn, addr = sock.accept()

#./tests/netperf_server server.config 1 10
command      = "./tcp_rr"

payload_size = 16384

while payload_size < 32000:
    flows = 10    
    while flows < 150:
        process = subprocess.run([command, "-F", str(flows), "-B", str(payload_size)])
        flows += 20
    payload_size *= 2

sock.close()
