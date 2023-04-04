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
conn, addr = sock.accept()

#sudo ./tcp_stream -F 10 -T 1

su        = "sudo"
command   = "./tcp_stream"
client    = "-c"
host      = "-H"
host_ip   = "128.110.219.182"
f_command = "-F"
t_command = "-T"
# nflows    = [10, 50]
nflows     = [1000, 5000, 10000, 15000, 20000, 30000, 40000, 50000, 75000, 100000]
# nthreads  = "1"
nthreads  = [5, 10, 25, 50, 75]
# nthreads  = [25, 50, 75]
# nthreads = [50, 75]
# kthreads  = [20, 30, 40]
kthreads = [10, 20, 30, 40]
# sthreads  = [25, 50, 75 , 100]
sthreads = [5, 10, 20, 25, 50]
encoding = 'utf-8'

# command      = "tests/netperf_server"
# config       = "server.config"
# nthreads     = "1"
# payload_size = 10 

a = '''# an example runtime config file
host_addr 10.10.1.1
host_netmask 255.255.255.0
host_gateway 192.168.1.1
runtime_kthreads {}
#runtime_spinning_kthreads 2
#runtime_guaranteed_kthreads 2
runtime_priority lc
#preferred_socket 0
#disable_watchdog 1
#enable_directpath 1
#host_mtu 8000'''

# fi.write(a)


ckpt_flows = 40000
ckpt_kthreads = 30
ckpt_sthreads = 25
ckpt_cthreads = 75

for flows in nflows:
    if flows < ckpt_flows:
        continue
    for kt in kthreads:
        if flows == ckpt_flows and kt < ckpt_kthreads:
            continue
        fi = open("delete.config", "w")
        print(a.format(kt))
        fi.write(a.format(kt))
        fi.flush()
        fi.close()
        for threads in sthreads:
            if flows == ckpt_flows and kt == ckpt_kthreads and threads < ckpt_sthreads:
                continue
            for client_threads in nthreads:
                if flows == ckpt_flows and  kt == ckpt_kthreads and threads == ckpt_sthreads and client_threads <= ckpt_cthreads:
                    continue
                if threads > flows or client_threads > flows:
                    break
                while client_threads < flows/16384:
                    client_threads = client_threads + 1
                i = 0
                timeout = 5
                while i < 3:
                    print("Flows: {} Kthreads: {} Server_Threads: {} Client_Threads: {} Iteration round: {}\n".format(flows, kt, threads, client_threads, i))
                    try:
                        data_from_client = conn.recv(1024)
                        if client_threads >= 50:
                            print("timeout: ", timeout)
                            time.sleep(timeout)
                        process = subprocess.run([su, command, f_command, str(flows), t_command, str(client_threads)], check=True)
                        # data = "started"
                        # conn.send(data.encode())
                        print("waiting to receive")
                        data_from_client = conn.recv(1024)
                        # kill = subprocess.Popen(["kill", str(process.pid)])

                        data = "killed"
                        conn.send(data.encode())
                        i = i + 1
                    except:
                        timeout = timeout*2
                        print("ERROR!!!!!!")
                        data = "retry"
                        conn.send(data.encode())


#conn.send(data_from_client)
sock.close()

