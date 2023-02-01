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

#sock.listen()
#conn, addr = sock.accept()

#./tests/netperf_server server.config 1 10
command      = "./tcp_stream"
encoding = 'utf-8'
payload_size = 16384

file1 = open("test_neper_100g.txt", "w")

flows = 10

while flows < 3200:
    process = subprocess.check_output([command, "-c", "-H", "10.10.1.1", "-F", str(flows), "-T", str(1)])
    output = process.decode(encoding)
    for line in output.splitlines():
            if "remote_throughput" in line:
                a = line.split("=")[1]
                a = float(a)
    
    file1.write("num_flows {}     ".format(flows))
    file1.write("Throughput: {}\n".format(a/1000000000))
    file1.write("-----------------------\n")
    file1.flush()
    flows += 1000


sock.close()
