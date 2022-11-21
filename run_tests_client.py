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
encoding = 'utf-8'
payload_size = 16384

file1 = open("test_neper_16000.txt", "w")

while payload_size < 32000:
    file1.write("payload_size: {}\n".format(payload_size))
    flows = 10
    while flows < 151:
        process = subprocess.check_output([command, "-c", "-H", "128.110.218.45", "-F", str(flows), "-B", str(payload_size)])
        output = process.decode(encoding)
        for line in output.splitlines():
                if "throughput" in line:
                    a = line.split("=")[1]
                    a = float(a)
    
        file1.write("num_flows {}     ".format(flows))
        file1.write("Throughput: {}\n".format(a))
        file1.write("-----------------------\n")
        file1.flush()
        flows += 20

    payload_size *= 2

sock.close()
