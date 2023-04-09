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
        subprocess.run("sudo pkill -9 iokerneld", shell=True)

        subprocess.run("sudo pkill -9 tcp_stream", shell=True)
        subprocess.run("sudo pkill -9 tcp_stream", shell=True)

        tm.sleep(timeout)
        timeout = timeout*2
        subprocess.Popen("sudo ../iokerneld", shell=True)
        tm.sleep(5)

        data = "don0"
        conn.send(data.encode())

        tm.sleep(3)
        subprocess.run("sudo ./tcp_stream -F 1000 -T 25", shell=True)

        data_from_client = conn.recv(4)
        print("DATA FROMCLIENT: ", data_from_client)
        if data_from_client.decode() == "don0":
            return
        

# def restart_iokernel():
#     subprocess.run("sudo pkill -9 iokerneld", shell=True)
#     subprocess.run("sudo pkill -9 iokerneld", shell=True)
#     subprocess.run("sudo pkill -9 iokerneld", shell=True)

#     subprocess.run("sudo pkill -9 tcp_stream", shell=True)
#     subprocess.run("sudo pkill -9 tcp_stream", shell=True)

#     time.sleep(5)
#     subprocess.Popen("sudo ../iokerneld", shell=True)
#     time.sleep(5)

#     data = "start"
#     conn.send(data.encode())

#     subprocess.run("sudo ./tcp_stream -F 1000 -T 25", shell=True)

#     subprocess.run("sudo pkill -9 tcp_stream", shell=True)
#     subprocess.run("sudo pkill -9 iokerneld", shell=True)
#     time.sleep(50)
#     subprocess.Popen("sudo ../iokerneld", shell=True)
#     time.sleep(5)



control_plane_port = int(sys.argv[1])

#creating the socket
sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

#binding and listening to all interfaces
serv_addr = ("0.0.0.0", control_plane_port)
sock.bind(serv_addr)

sock.listen()
conn, addr = sock.accept()

# conn.recv(1024)
restart_iokernel()
# quit()

#sudo ./tcp_stream -F 10 -T 1
su        = "sudo"
command   = "./tcp_stream"
client    = "-c"
host      = "-H"
host_ip   = "128.110.219.182"
f_command = "-F"
t_command = "-T"

nflows            = [1000, 5000, 20000, 50000, 75000, 100000]
cthreads          = [5, 10, 25, 50]
server_kthreads   = [5, 10, 20, 30, 40]
client_kthreads   = [10, 20, 30, 40]
sthreads          = [5, 10, 25, 50]
encoding          = 'utf-8'

# command      = "tests/netperf_server"
# config       = "server.config"
# nthreads     = "1"
# payload_size = 10 

a = '''# an example runtime config file
host_addr 10.10.1.1
host_netmask 255.255.255.0
host_gateway 192.168.1.1
runtime_kthreads {}
runtime_priority lc
enable_directpath 1
#host_mtu 8000'''

ckpt_flows = int(sys.argv[2])
ckpt_s_kthreads = int(sys.argv[3])
ckpt_c_kthreads = int(sys.argv[4])
ckpt_sthreads = int(sys.argv[5])
ckpt_cthreads = int(sys.argv[6])

print("ckpt_flows", ckpt_flows)

for flows in nflows:
    if flows < ckpt_flows:
        continue

    for skt in server_kthreads:    
        if flows == ckpt_flows and skt < ckpt_s_kthreads:
            continue

        for ckt in client_kthreads:
            if flows == ckpt_flows and skt == ckpt_s_kthreads and ckt < ckpt_c_kthreads:
                continue

            fi = open("delete.config", "w")
            print(a.format(ckt))
            fi.write(a.format(ckt))
            fi.flush()
            fi.close()
            
            for server_threads in sthreads:
                if flows == ckpt_flows and skt == ckpt_s_kthreads and ckt == ckpt_c_kthreads and server_threads < ckpt_sthreads:
                    continue

                for client_threads in cthreads:
                    if flows == ckpt_flows and  skt == ckpt_s_kthreads and ckt == ckpt_c_kthreads and server_threads == ckpt_sthreads and client_threads <= ckpt_cthreads:
                        continue

                    while client_threads < flows/16384:
                        client_threads = client_threads + 1

                    i = 0
                    timeout = 5
                    while i < 3:
                        print("Flows: {} SKthreads: {} CKthreads: {} Server_Threads: {} Client_Threads: {} Iteration round: {}\n".format(flows, skt, ckt, server_threads, client_threads, i))
                        try:
                            data_from_client = conn.recv(4)

                            #if client_threads >= 50:
                            #    time.sleep(timeout)

                            process = subprocess.run([su, command, f_command, str(flows), t_command, str(client_threads)], check=True, timeout=400)

                            print("waiting to receive")
                            data_from_client = conn.recv(4)

                            if data_from_client.decode() == "don1":
                                data = "don1"
                                conn.send(data.encode())
                                restart_iokernel()
                                continue
                            # kill = subprocess.Popen(["kill", str(process.pid)])

                            data = "don0"
                            conn.send(data.encode())

                            data_from_client = conn.recv(4)
                            print("EVERYTHING GOOD: ", data_from_client)
                            if data_from_client.decode() == "don1":
                                restart_iokernel()
                                continue
                            i = i + 1

                        except:
                            timeout = timeout*2
                            print("ERROR!!!!!!")
                            dummy = conn.recv(4)
                            data = "don1"
                            conn.send(data.encode())
                            restart_iokernel()


#conn.send(data_from_client)
sock.close()

