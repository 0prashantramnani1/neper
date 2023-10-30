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
        subprocess.Popen("sudo ../iokerneld simple", shell=True)
        tm.sleep(5)
        start_signal = sock.recv(4)
        tm.sleep(5)
        a = subprocess.check_output("sudo ./tcp_stream -c -H 192.168.1.30 -F 100 -T 2 --num-ports 5 -l 20", shell=True)
        tmp = get_throughput(a)
        if tmp > 1:
            data = "don0"
            sock.send(data.encode())
            return
        data = "don1"
        sock.send(data.encode())
        



serv_ip            = "130.207.125.113"
control_plane_port = int(sys.argv[1])

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
try:
    sock.connect((serv_ip, control_plane_port))
except:
    print("Connection to server failed")
    exit()

# quit()

#/sudo ./tcp_stream -c -H 10.10.1.1 -F 10 -T 1
#[config, nflows, nthreads, server_ip, time, payload, depth]
su        = "sudo"
command   = "./tcp_stream"
client    = "-c"
host      = "-H"
host_ip   = "192.168.1.30"
f_command = "-F"
t_command = "-T"
b_command = "-B"
l_command = "-l"
batch_command = "--batch-size"
exp_len   = 100
numports_command = "--num-ports"
server_threads = 1

nflows            = [10, 100, 1000]
time_quantum      = [1]
buffer_size       = [16384]
batch_size        = [16384]
uthreads          = [1, 5, 10]
kthreads          = [1, 2, 5, 24]
encoding          = 'utf-8'


config  = '''# an example runtime config file
host_addr 192.168.1.80
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

fi = open("sender.config", "w")
# print(config.format(buf))
fi.write(config.format(8))
fi.flush()
fi.close()

restart_iokernel()


for f in nflows:
    file1 = open("kegs/test_{}.txt".format(f), "a")
    for kthread in kthreads:
        fi = open("sender.config", "w")
        fi.write(config.format(kthread))
        fi.flush()
        fi.close()
        for uthread in uthreads:
            #restart_iokernel()
            timeout = 5
            avg     = []
            cyc     = []
            ins     = []
            dtlb    = []
            itlb    = []
            l3      = []
            i = 0
            print("Flows: {} KThreads: {} Uthreads: {} \n".format(f, kthread, uthread))
            while i < 1:
                try:
                    data = "don0"
                    sock.send(data.encode())
                    print("1")
                    tm.sleep(timeout)
                    process = subprocess.check_output([su, command, client, host, host_ip, f_command, str(f), 
                                t_command, str(uthread), numports_command, str(uthread), l_command, str(exp_len)])
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
                        if a/1000000000 < 0:
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
                            s = a[:index]
                            s = s.replace(",", "")
                            cyc.append(int(s))

                        if "instructions" in line:
                            a = line.replace(' ', '')
                            index = a.find('i')
                            s = a[:index]
                            s = s.replace(",", "")
                            ins.append(int(s))

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
                            s = a[:index]
                            s = s.replace(",", "")
                            l3.append(int(s))

            print(cyc[0])
            print(ins[0])
            #print(dtlb[0])
            #print(itlb[0])
            print(l3[0])
            for i in range(1):
                file1.write("IAS: Connections {} uthreads {} kthreads {} throughput {} CPU_Cyc {} CPU_Instr {} instr/cyc {} l3_misses {}\n".format(f, uthread, kthread, avg[i], cyc[i], ins[i], float(ins[i])/float(cyc[i]), l3[i]))
                file1.flush()


