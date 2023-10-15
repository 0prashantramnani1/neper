import sys
import subprocess
import socket
import time as tm

def get_throughput(output):
    output = process.decode(encoding)
    for line in output.splitlines():
        if "remote_throughput" in line:
            a = [int(s) for s in line.split('=') if s.isdigit()][0]
            print("a: ", a)
            return a/1000000000


def restart_iokernel():
    timeout = 20
    while 1:
        subprocess.run("sudo pkill -9 iokerneld", shell=True)
        subprocess.run("sudo pkill -9 iokerneld", shell=True)

        subprocess.run("sudo pkill -9 tcp_stream", shell=True)
        subprocess.run("sudo pkill -9 tcp_stream", shell=True)

        tm.sleep(timeout)
        timeout = timeout+5
        subprocess.Popen("sudo ../iokerneld", shell=True)
        tm.sleep(5)

        data = "don0"
        conn.send(data.encode())

        tm.sleep(3)
        subprocess.run("sudo ./tcp_stream -F 100 -T 5", shell=True)

        data_from_client = conn.recv(4)
        print("DATA FROMCLIENT: ", data_from_client)
        if data_from_client.decode() == "don0":
            return

control_plane_port = int(sys.argv[1])

#creating the socket
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

#binding and listening to all interfaces
serv_addr = ("0.0.0.0", control_plane_port)
sock.bind(serv_addr)

sock.listen()
conn, addr = sock.accept()
print("Connection Accepted")
# restart_iokernel()

#sudo ./tcp_stream -F 10 -T 1
su        = "sudo"
command   = "./tcp_stream"
client    = "-c"
host      = "-H"
host_ip   = "128.110.219.182"
f_command = "-F"
t_command = "-T"
b_command = "-B"
l_command = "-l"
ias       = "ias"
noht      = "noht"

#nflows            = {100:3, 1000:3, 10000:3}
nflows            = {100000:3}
kthreads          = {100:8, 1000:8, 10000:8, 100000:8, 200000:12}
batch_size        = [4096, 8192, 16384, 65536, 131072]
time_quantum      = [1]
buffer_size       = [4096, 16384, 65536, 131072]

# buffer_size       = [4096, 16384, 32768]
# batch_size        = [4096, 8192, 16384]
encoding          = 'utf-8'


config  = '''# an example runtime config file
host_addr 10.10.1.1
host_netmask 255.255.255.0
host_gateway 192.168.1.1
runtime_kthreads {}
#runtime_spinning_kthreads 2
runtime_guaranteed_kthreads {}
runtime_priority lc
#preferred_socket 0
#disable_watchdog 1
enable_directpath 1
#host_mtu 8000'''

fi = open("receiver.config", "w")
# print(config.format(buf, buf))
fi.write(config.format(8, 8))
fi.flush()
fi.close()

restart_iokernel()


for buf in buffer_size:    
    for ts in time_quantum:
        for f, t in nflows.items():
            for batch in batch_size:
                if batch > buf:
                    continue

                fi = open("receiver.config", "w")
                print(config.format(buf, buf))
                fi.write(config.format(kthreads[f], kthreads[f]))
                fi.flush()
                fi.close()

                i = 0
                timeout = 5
                print("BUF: {} batch: {} Flows: {} Client_Threads: {}\n".format(buf, batch, f, t))
                while i < 1:
                    try:
                        data_from_client = conn.recv(4)

                        process = subprocess.run([su, command, f_command, str(f), t_command, str(t), b_command, str(batch), l_command, str(400)], check=True)

                        print("waiting to receive")
                        data_from_client = conn.recv(4)

                        if data_from_client.decode() == "don1":
                            data = "don1"
                            conn.send(data.encode())
                            restart_iokernel()
                            continue

                        data = "don0"
                        conn.send(data.encode())

                        data_from_client = conn.recv(4)
                        print("EVERYTHING GOOD: ", data_from_client)
                        if data_from_client.decode() == "don1":
                            restart_iokernel()
                            continue
                        i = i + 1

                    except:
                        timeout = timeout+5
                        print("ERROR!!!!!!")
                        dummy = conn.recv(4)
                        data = "don1"
                        conn.send(data.encode())
                        restart_iokernel()

