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
        subprocess.Popen("sudo ../iokerneld simple", shell=True)
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

nflows            = [10, 100, 1000]
time_quantum      = [1]
buffer_size       = [16384]
batch_size        = [16384]
uthreads          = [1, 5, 10]
kthreads          = [1, 2, 5, 24]

# buffer_size       = [4096, 16384, 32768]
# batch_size        = [4096, 8192, 16384]
encoding          = 'utf-8'


config  = '''# an example runtime config file
host_addr 192.168.1.30
host_netmask 255.255.255.0
host_gateway 192.168.1.1
runtime_kthreads {}
#runtime_spinning_kthreads 2
#runtime_guaranteed_kthreads 2
runtime_priority lc
#preferred_socket 0
#disable_watchdog 1
enable_directpath 1
#host_mtu 8000
directpath_pci 0000:3b:00.0'''

fi = open("receiver.config", "w")
# print(config.format(8))
fi.write(config.format(8))
fi.flush()
fi.close()

restart_iokernel()


for f in nflows:
    for kthread in kthreads:
        fi = open("receiver.config", "w")
        fi.write(config.format(kthread))
        fi.flush()
        fi.close()
        for uthread in uthreads:
            #restart_iokernel()
            i = 0
            timeout = 5
            print("Flows: {} KThreads: {} Uthreads: {} \n".format(f, kthread, uthread))
            while i < 1:
                try:
                    data_from_client = conn.recv(4)

                    process = subprocess.run([su, command, f_command, str(f), t_command, str(uthread), b_command, str(batch_size[0]), l_command, str(100)], check=True)

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


