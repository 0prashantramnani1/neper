import sys
import subprocess
import time as tm
import socket

serv_ip            = "128.110.219.182"
control_plane_port = int(sys.argv[1])

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
try:
    sock.connect((serv_ip, control_plane_port))
except:
    print("Connection to server failed")
    exit()




#/sudo ./tcp_stream -c -H 10.10.1.1 -F 10 -T 1
#[config, nflows, nthreads, server_ip, time, payload, depth]
su        = "sudo"
command   = "./tcp_stream"
client    = "-c"
host      = "-H"
host_ip   = "10.10.1.1"
f_command = "-F"
t_command = "-T"
numports_command = "--num-ports"
nflows     = [1000, 5000, 10000, 15000, 20000, 30000, 40000, 50000, 75000, 100000]
# nthreads  = "1"
nthreads  = [5, 10, 20, 25, 50, 75]
# nthreads  = [25, 50, 75]
# nthreads = [50, 75]
# kthreads  = [20, 30, 40]
kthreads = [10, 20, 30, 40]
# sthreads  = [25, 50, 75 , 100]
sthreads = [5, 10, 20, 25, 50, 75, 100]
encoding = 'utf-8'

a = '''# an example runtime config file
host_addr 10.10.1.2
host_netmask 255.255.255.0
host_gateway 192.168.1.1
runtime_kthreads {}
#runtime_spinning_kthreads 2
#runtime_guaranteed_kthreads 2
runtime_priority lc
#preferred_socket 0
#disable_watchdog 1
enable_directpath 1
#host_mtu 8000'''

# fi = open("delete.config", "w")
# fi.write(a)
# exit()

ckpt_flows = 30000
ckpt_kthreads = 30
ckpt_sthreads = 0
ckpt_cthreads = 0

for flows in nflows:
    file1 = open("results/test_{}_rdp.txt".format(flows), "a")
    if flows < ckpt_flows:
        continue
    for kt in kthreads:
        if flows == ckpt_flows and kt < ckpt_kthreads:
            continue
        for threads in sthreads:
            if flows == ckpt_flows and kt == ckpt_kthreads and threads < ckpt_sthreads:
                continue
            for client_threads in nthreads:
                if threads > flows or client_threads > flows:
                    break
                if flows == ckpt_flows and  kt == ckpt_kthreads and threads == ckpt_sthreads and client_threads <= ckpt_cthreads:
                    continue
                avg = []
                i = 0
                timeout = 5
                while i < 3:
                    print("Flows: {} Kthreads: {} Server_Threads: {} Client_Threads: {} Iteration round: {}\n".format(flows, kt, threads, client_threads, i))
                    try:
                        data = "start"
                        sock.send(data.encode())
                        # print("waiting to receive")
                        # data_from_server = sock.recv(1024)
                        # print("received")
                        if client_threads >= 50:
                            tm.sleep(timeout)
                        tm.sleep(5)
                        process = subprocess.check_output([su, command, client, host, host_ip, f_command, str(flows), t_command, str(threads), numports_command, str(client_threads)], timeout=60)
                        output = process.decode(encoding)
                    except:
                        timeout = timeout*2
                        file1.write("SEG FAULT!!!!!!!!!!\n")
                        output = ""

                    data = "done"
                    sock.send(data.encode())
                    kill_signal = sock.recv(1024)

                    if(kill_signal.decode() == "retry"):
                        continue

                    for line in output.splitlines():
                        if "remote_throughput" in line:
                            a = [int(s) for s in line.split('=') if s.isdigit()][0]
                            print("a: ", a)
                            avg.append(a/1000000000)
                            i += 1

                    

                mean = sum(avg) / len(avg)
                variance = sum([((x - mean) ** 2) for x in avg]) / len(avg)
                file1.write("kthreads {} server_threads {} client_threads {} avg_throughput {} standard_deviation {} Max {} Min {}\n".format(kt, threads, client_threads, mean, variance ** 0.5, max(avg), min(avg)))
                file1.flush()
