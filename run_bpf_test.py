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
        a = subprocess.check_output("sudo ./tcp_stream -c -H 10.10.1.1 -F 100 -T 2 --num-ports 5 -l 20", shell=True)
        tmp = get_throughput(a)
        if tmp > 1:
            data = "don0"
            sock.send(data.encode())
            return
        data = "don1"
        sock.send(data.encode())
        



serv_ip            = "128.110.219.168"
control_plane_port = int(sys.argv[1])

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
try:
    sock.connect((serv_ip, control_plane_port))
except:
    print("Connection to server failed")
    exit()

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
b_command = "-B"
l_command = "-l"
batch_command = "--batch-size"
exp_len   = 250
numports_command = "--num-ports"
server_threads = 1

#nflows            = {100:3, 1000:3, 10000:3}
nflows            = {100000:3}
time_quantum      = [1]
buffer_size       = [4096, 16384, 65536, 131072]
batch_size        = [4096, 8192, 16384, 65536, 131072]
# buffer_size       = [4096, 16384, 32768]
# batch_size        = [4096, 8192, 16384]
encoding          = 'utf-8'


file1 = open("600k_12ut_r.txt", "a")

for buf in buffer_size:
    file1 = open("BPF/test_{}_bpf_batch.txt".format(buf), "a")
    for ts in time_quantum:
        for f, t in nflows.items():
            for batch in batch_size:
                if batch > buf:
                    continue

                timeout = 5
                avg     = []
                cyc     = []
                ins     = []
                dtlb    = []
                itlb    = []
                l3      = []
                i = 0
                print("BUF: {} batch: {} Flows: {} Client_Threads: {}\n".format(buf, batch, f, t))
                while i < 1:
                    try:
                        data = "don0"
                        sock.send(data.encode())
                        print("1")
                        tm.sleep(timeout)
                        process = subprocess.check_output([su, command, client, host, host_ip, f_command, str(f), 
                                    t_command, str(server_threads), numports_command, str(t), b_command, str(buf), batch_command, str(batch)])
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

                    with open("perf_output.txt") as perf_file:
                        lines = perf_file.readlines()
                        for line in lines:
                            if "cycles" in line:
                                print(line)
                                a = line.replace(' ', '')
                                print(a)
                                index = a.find('c')
                                cyc.append(int(a[:index]))

                            if "instructions" in line:
                                a = line.replace(' ', '')
                                index = a.find('i')
                                ins.append(int(a[:index]))

                            if "dTLB-load-misses" in line:
                                a = line.replace(' ', '')
                                index = a.find('d')
                                dtlb.append(int(a[:index]))
                            
                            if "iTLB-load-misses" in line:
                                a = line.replace(' ', '')
                                index = a.find('i')
                                itlb.append(int(a[:index]))
                            
                            if "cache-misses" in line:
                                a = line.replace(' ', '')
                                index = a.find('c')
                                l3.append(int(a[:index]))

                print(cyc[0])
                print(ins[0])
                print(dtlb[0])
                print(itlb[0])
                print(l3[0])
                for i in range(1):
                    # mean = sum(avg) / len(avg)
                    # variance = sum([((x - mean) ** 2) for x in avg]) / len(avg)
                    file1.write("Connections {} batch_size {} throughput {} CPU_Cyc {} CPU_Instr {} instr/cyc {} l3_misses {} dtlb_misses {} itlb_misses {}\n".format(f, batch, avg[i], cyc[i], ins[i], float(ins[i])/float(cyc[i]), l3[i], dtlb[i], itlb[i]))
                    file1.flush()

