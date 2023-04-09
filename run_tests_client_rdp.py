import sys
import subprocess
import time as tm
import socket

def get_throughput(a):
    encoding          = 'utf-8'
    output = a.decode(encoding)
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
        start_signal = sock.recv(4)
        tm.sleep(5)
        a = subprocess.check_output("sudo ./tcp_stream -c -H 10.10.1.1 -F 1000 -T 25 --num-ports 25", shell=True)
        tmp = get_throughput(a)
        if tmp > 1:
            data = "don0"
            sock.send(data.encode())
            return
        data = "don1"
        sock.send(data.encode())
        



serv_ip            = "128.110.219.182"
control_plane_port = int(sys.argv[1])

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
try:
    sock.connect((serv_ip, control_plane_port))
except:
    print("Connection to server failed")
    exit()

# sock.recv(1024x)

restart_iokernel()
# quit()

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

nflows            = [1000, 5000, 20000, 50000, 75000, 100000]
cthreads          = [5, 10, 25, 50]
server_kthreads   = [5, 10, 20, 30, 40]
client_kthreads   = [10, 20, 30, 40]
sthreads          = [5, 10, 25, 50]
encoding          = 'utf-8'

config = '''# an example runtime config file
host_addr 10.10.1.2
host_netmask 255.255.255.0
host_gateway 192.168.1.1
runtime_kthreads {}
runtime_priority lc
#enable_directpath 1
#host_mtu 8000'''

ckpt_flows = int(sys.argv[2])
ckpt_s_kthreads = int(sys.argv[3])
ckpt_c_kthreads = int(sys.argv[4])
ckpt_sthreads = int(sys.argv[5])
ckpt_cthreads = int(sys.argv[6])



for flows in nflows:
    if flows < ckpt_flows:
        continue

    file1 = open("new_results/test_{}_rdp.txt".format(flows), "a")
    
    for skt in server_kthreads:    
        if flows == ckpt_flows and skt < ckpt_s_kthreads:
            continue

        fi = open("server.config", "w")
        print(config.format(skt))
        fi.write(config.format(skt))
        fi.flush()
        fi.close()

        for ckt in client_kthreads:
            if flows == ckpt_flows and skt == ckpt_s_kthreads and ckt < ckpt_c_kthreads:
                continue

            for server_threads in sthreads:
                if flows == ckpt_flows and skt == ckpt_s_kthreads and ckt == ckpt_c_kthreads and server_threads < ckpt_sthreads:
                    continue

                for client_threads in cthreads:
                    if flows == ckpt_flows and skt == ckpt_s_kthreads and ckt == ckpt_c_kthreads and server_threads == ckpt_sthreads and client_threads <= ckpt_cthreads:
                        continue

                    while client_threads < flows/16384:
                        client_threads = client_threads + 1

                    avg = []
                    i = 0
                    timeout = 5
                    while i < 3:
                        print("Flows: {} SKthreads: {} CKthreads: {} Server_Threads: {} Client_Threads: {} Iteration round: {}\n".format(flows, skt, ckt, server_threads, client_threads, i))
                        try:
                            data = "don0"
                            sock.send(data.encode())

                            #if client_threads >= 50:
                            #    tm.sleep(
                            tm.sleep(timeout)
                            process = subprocess.check_output([su, command, client, host, host_ip, f_command, str(flows), t_command, str(server_threads), numports_command, str(client_threads)], timeout=400)
                            output = process.decode(encoding)

                            data = "don0"
                        except:
                            timeout = timeout*2
                            print("ERROR!")
                            file1.write("Error\n")
                            output = ""
                            data = "don1"
                            #restart_iokernel()
                        
                        sent = sock.send(data.encode())
                        print("sent: ", sent)
                        kill_signal = sock.recv(4)

                        if kill_signal.decode() == "don1" or data == "don1":
                            restart_iokernel()
                            continue

                        for line in output.splitlines():
                            if "remote_throughput" in line:
                                a = [int(s) for s in line.split('=') if s.isdigit()][0]
                                print("a: ", a)
                                if a/1000000000 < 1:
                                    thru = "don1"
                                    sock.send(thru.encode())
                                    restart_iokernel()
                                    continue
                                thru = "don0"
                                sock.send(thru.encode())
                                avg.append(a/1000000000)
                                i += 1

                    

                    mean = sum(avg) / len(avg)
                    variance = sum([((x - mean) ** 2) for x in avg]) / len(avg)
                    file1.write("skthreads {} ckthreads {} server_threads {} client_threads {} avg_throughput {} standard_deviation {} Max {} Min {}\n".format(skt, ckt, server_threads, client_threads, mean, variance ** 0.5, max(avg), min(avg)))
                    file1.flush()
