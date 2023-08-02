import sys
import subprocess
import time as tm
import socket

def get_throughput(a):
    # print(a)
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

        subprocess.run("sudo pkill -9 tcp_stream", shell=True)
        subprocess.run("sudo pkill -9 tcp_stream", shell=True)

        tm.sleep(timeout)
        timeout = timeout + 5
        subprocess.Popen("sudo ../iokerneld", shell=True)
        tm.sleep(5)
        start_signal = sock.recv(4)
        tm.sleep(5)
        a = subprocess.check_output("sudo ./tcp_stream -c -H 10.10.1.1 -F 10000 -T 2 --num-ports 5 -l 20", shell=True)
        tmp = get_throughput(a)
        if tmp > 1:
            data = "don0"
            sock.send(data.encode())
            return
        data = "don1"
        sock.send(data.encode())
        



serv_ip            = "128.110.219.165"
control_plane_port = int(sys.argv[1])

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
try:
    sock.connect((serv_ip, control_plane_port))
except:
    print("Connection to server failed")
    exit()

# restart_iokernel()
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
server_threads = 1

# nflows            = {100:3, 500:3, 1000:3, 10000:3, 50000:3, 100000:3}
nflows            = {600000:12}
# nflows            = {10000:3, 20000:3, 30000:4, 40000:8}
# time_quantum      = [15, 20, 25, 30, 40, 50, 60, 70, 80, 90, 100]
time_quantum      = [40000000000]
receiver_kthreads = [20, 22, 24, 26, 28, 30, 32, 34, 38, 42, 44, 46]
encoding          = 'utf-8'

config = '''# an example runtime config file
host_addr 10.10.1.2
host_netmask 255.255.255.0
host_gateway 192.168.1.1
runtime_kthreads 2
runtime_guaranteed_kthreads 2
runtime_spinning_kthreads 2
runtime_priority lc
#enable_directpath 1
#host_mtu 8000
runtime_quantum_us {}'''
restart_iokernel()



file1 = open("600k_12ut_r.txt", "a")

for rkt in receiver_kthreads:
    for ts in time_quantum:
        fi = open("sender.config", "w")
        print(config.format(ts))
        fi.write(config.format(ts))
        fi.flush()
        fi.close()

        for f, t in nflows.items():
            timeout = 5
            avg = []
            i = 0
            print("RKT: {} Flows: {} Client_Threads: {}\n".format(rkt, f, t))
            while i < 1:
                try:
                    data = "don0"
                    sock.send(data.encode())

                    tm.sleep(timeout)
                    process = subprocess.check_output([su, command, client, host, host_ip, f_command, str(f), t_command, str(server_threads), numports_command, str(t)])
                    output = process.decode(encoding)

                    data = "don0"
                except:
                    timeout = timeout + 5
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
            file1.write("rkt {} Flows {} client_threads {} avg_throughput {} standard_deviation {} Max {} Min {}\n".format(rkt, f, t, mean, variance ** 0.5, max(avg), min(avg)))
            file1.flush()
        

