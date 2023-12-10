#!/usr/bin/python3
import os
import re
import sys
import csv
import yaml
import socket
import argparse
import textwrap
import subprocess
from datetime import datetime
from collections import namedtuple

Results = namedtuple('Results', 'lines summary threads')

def divider(name):
    print('--------------------------------------------------------------')
    print(f'{name}...')

class Dmesg:
    def read():
        result = subprocess.run(['dmesg'], stdout=subprocess.PIPE, universal_newlines=True)
        return result.stdout.rstrip().split('\n')

    def last_timestamp():
        last_line = Dmesg.read()[-1]
        match = re.search(r'\[\s*([0-9.]+)\s*\]', last_line)
        if not match:
            return None
        return match.group(1)

    def lines_since(start):
        lines = Dmesg.read()
        last_match_index = None
        for i,line in enumerate(lines):
            if re.search(f'{start}', line):
                last_match_index = i

        if last_match_index is None:
            return lines

        return lines[last_match_index+1:]

def read_kio_version():
    with open('/sys/module/kio/version', 'r') as f:
        return f.readline().rstrip()

class Kio():
    def __init__(self, num_threads, runtime_seconds):
        self.im_root = os.getuid() == 0
        self.num_threads = num_threads
        self.runtime_seconds = runtime_seconds

        self.conf_names = ['num_threads', 'runtime_seconds']
        self.thread_names = ['block_size', 'burst_delay', 'burst_finish',
                'offset_high', 'offset_low', 'offset_random', 'offset_stride',
                'queue_depth', 'read_burst', 'read_mix_percent',
                'read_sleep_usec', 'write_burst', 'write_sleep_usec']

        cur = self.read('num_threads')
        #print(f"cur={cur} new={num_threads}")
        if cur is None or ( cur>0 and cur != num_threads ):
            divider('Reloading')
            self.reload()

        if cur != num_threads:
            self.write('num_threads', num_threads);

        self.write('runtime_seconds', runtime_seconds);

        self.version = read_kio_version()

    def reload(self):
        subprocess.run(['make', 'reload'], check=True)

    def get_global_config(self):
        res = {}
        for k in self.conf_names:
            res[k] = self.read(k)
        return res

    def get_thread_config(self, tid):
        res = {}
        for k in self.thread_names:
            res[k] = self.read(tid, k)
        return res

    def set_thread_config(self, tid, conf):
        for k in self.thread_names:
            if k in conf:
                self.write(tid, k, conf[k])

    def get_config(self):
        conf = self.get_global_config()

        threads = dict()
        for tid in range(int(conf['num_threads'])):
            threads[tid] = self.get_thread_config(tid)

        result = {
            'global': conf,
            'threads': threads
        }

        return result

    def conf_file(self, *components):
        if not len(components):
            raise ValueError('no path provided')
        path='/sys/kernel/kio'
        for p in components:
            path += '/' + str(p)
        return path

    def read(self, *args):
        path = self.conf_file(*args)
        if not os.path.exists(path):
            return None
        with open(path, 'r') as f:
            v = f.readline().rstrip()
            try:
                v = int(v)
            except:
                pass
            return v

    def write(self, *args):
        path = self.conf_file(*args[:-1])
        value = args[-1]
        if self.im_root:
            with open(path, 'w') as f:
                return f.write(str(value))
        else:
            subprocess.run(['sudo', 'sh' , '-c',
                f'echo {value} > {path}'], check=True)

    def configure(self, *configs):
        for config in configs:
            pass

    def run(self):
        start = Dmesg.last_timestamp()
        self.write('run_workload', 1);
        lines = Dmesg.lines_since(start)

        if len(lines) < 1:
            raise ValueError('did not find any data in dmesg output')

        return self.parse_results(lines)

    def parse_results(self, lines):
        summary = None
        threads = dict()

        reth = re.compile(r'thread\[([0-9]+)\]: completed=([0-9]+) lat=([0-9.]+)\(([0-9.]+)\+([0-9.]+)\) iops=([0-9]+) MB/s=([0-9.]+)')
        resm = re.compile(r'summary: completed=([0-9]+) lat=([0-9.]+)\(([0-9.]+)\+([0-9.]+)\) iops=([0-9]+) MB/s=([0-9.]+)')

        for line in lines:
            match = reth.search(line)
            if match:
                tid = int(match.group(1))
                thread = {
                        'completed': int(match.group(2)),
                        'lat_usec': float(match.group(3)),
                        'slat_usec': float(match.group(4)),
                        'clat_usec': float(match.group(5)),
                        'iops': float(match.group(6)),
                        'bw_MBps': float(match.group(7)) }
                threads[tid] = thread
                continue
            match = resm.search(line)
            if match:
                summary = {
                        'completed': int(match.group(1)),
                        'lat_usec': float(match.group(2)),
                        'slat_usec': float(match.group(3)),
                        'clat_usec': float(match.group(4)),
                        'iops': float(match.group(5)),
                        'bw_MBps': float(match.group(6)) }

        if summary is None:
            raise ValueError('did not find \'summary\' data in dmesg output')

        if len(threads) < 1:
            raise ValueError('did not find \'thread\' data in dmesg output')

        results = Results(lines, summary, threads)
        return results

def main(args):

    if args.read_config:
        with open(args.read_config, 'r') as f:
            conf = yaml.safe_load(f)

        if not args.num_threads:
            args.num_threads = conf['global']['num_threads']

        if not args.runtime_seconds:
            args.runtime_seconds = conf['global']['runtime_seconds']

    else:
        if not args.num_threads:
            args.num_threads = 1

        if not args.runtime_seconds:
            args.runtime_seconds = 5

    kio = Kio(args.num_threads, args.runtime_seconds)

    print(f'KIO version {kio.version}')

    if args.read_config:
        for tid in range(args.num_threads):
            if tid in conf['threads']:
                tconf = conf['threads'][tid]
                kio.set_thread_config(tid, tconf)

    for k in kio.thread_names:
        val = getattr(args, k, None)
        if not val:
            #print(f'# no {k}')
            continue

        #print(f'# {k} = {val}')
        for tid in range(args.num_threads):
            kio.write(tid, k, val)

    conf = kio.get_config()
    if args.generate_config:
        print(f'Generating config in {args.generate_config}')
        with open(args.generate_config, 'w') as f:
            yaml.dump(conf, f, indent=4, width=200, default_flow_style=False)
        sys.exit(0)

    divider('Current config')
    print(yaml.dump(conf, indent=4, width=200, default_flow_style=False))

    divider('Running')
    start = datetime.now()
    results = kio.run()

    divider('Results')
    print(yaml.dump(results, indent=4, width=200, default_flow_style=False))

    if not args.output_yaml and not args.output_csv:
        return

    timestamp = start.strftime("%Y/%m/%d %H:%M:%S")
    hostname = socket.gethostname()

    if args.output_yaml:
        everything = {
                'system': { 'timestamp': timestamp, 'hostname': hostname, 'kio_version': kio.version },
                'config': conf,
                'results': { 'summary': results.summary, 'threads': results.threads }
        }
        with open(args.output_yaml, 'w') as f:
            yaml.dump(everything, f, indent=4, width=200, default_flow_style=False)

    if args.output_csv:
        columns = [ 'timestamp', 'hostname', 'kio_version' ]
        values = [ timestamp, hostname, kio.version ]

        for k,v in conf['global'].items():
            columns.append(k)
            values.append(v)

        num_threads = int(conf['global']['num_threads'])
        for k in kio.thread_names:
            vs = []
            for tid in range(num_threads):
                tconf = conf['threads'][tid]
                v = str(tconf[k])
                if not v in vs:
                    vs.append(v)

            columns.append(k)
            if not len(vs):
                values.append('')
            else:
                values.append('/'.join(vs))

        for k,v in results.summary.items():
            columns.append(k)
            values.append(v)

        write_mode = 'w'
        if os.path.exists(args.output_csv):
            with open(args.output_csv, 'r', newline='') as file:
                reader = csv.reader(file)
                existing_headers = list(next(reader, None))

            if existing_headers != columns:
                print(f'columns:  {columns}')
                print(f'existing: {existing_headers}')
                raise ValueError("CSV file cannot be appended to; it has different column names")

            write_mode = 'a'

        with open(args.output_csv, write_mode, newline='') as file:
            writer = csv.writer(file)

            if write_mode == 'w':
                writer.writerow(columns)

            writer.writerow(values)

EXAMPLES = """
Run a one off configuration.  If multiple threads are used, they will use
the same configuration.

# {0} --num-threads 1 \\
        --runtime 5 \\
        --block-size 4096 \\
        --offset-stride 4096 \\
        --queue-depth 10 \\
        --offset-random 1 \\
        --read-mix-percent 100 \\
        --offset-high $((0xFFFFFFFF)) \\
        --read-burst 100 \\
        --read-mix-percent 100

Generate a config file, edit the details, and run it:

# {0} --num-threads 5 --generate-config config.yaml
# vim config.yaml
# {0} --runtime 5 --config config.yaml

"""

if __name__ == "__main__":

    class CustomFormatter(argparse.HelpFormatter):
        def __init__(self, prog):
            super().__init__(prog, max_help_position=50, width=120)

    parser = argparse.ArgumentParser(description="sweep for kio", formatter_class=CustomFormatter)

    group = parser.add_argument_group('Info')
    group.add_argument('--examples', action='store_true', help='show examples')
    group.add_argument('--version', action='store_true', help='show version')

    group = parser.add_argument_group('Run config')
    group.add_argument('-t', '--num-threads', dest='num_threads', metavar='NUM', type=int, help='number of threads')
    group.add_argument('-s', '--runtime', dest='runtime_seconds', metavar='SEC', type=int, help='seconds to run for')

    group = parser.add_argument_group('Workload config')
    group.add_argument('--bs', '--block-size',        dest='block_size',       metavar='N', type=int)
    group.add_argument('--bd', '--burst-delay',       dest='burst_delay',      metavar='N', type=int, help='0 delay after each IO, 1 delay after each burst')
    group.add_argument('--bf', '--burst-finish',      dest='burst_finish',     metavar='N', type=int, help='1 to wait for QD to drain when switching directions')
    group.add_argument('--ol', '--offset-low',        dest='offset_low',       metavar='N', type=int, help='lowest offset for IOs')
    group.add_argument('--oh', '--offset-high',       dest='offset_high',      metavar='N', type=int, help='highest offset for IOs')
    group.add_argument('--or', '--offset-random',     dest='offset_random',    metavar='N', type=int, help='1 to generate random offsets')
    group.add_argument('--os', '--offset-stride',     dest='offset_stride',    metavar='N', type=int, help='when not-random, increment offset after each IO by this')
    group.add_argument('--qd', '--queue-depth',       dest='queue_depth',      metavar='N', type=int, help='dispatch no more than this number of IOs concurrently')
    group.add_argument('--rm', '--read-mix-percent',  dest='read_mix_percent', metavar='N', type=int, help='0..100 percent read bursts')
    group.add_argument('--rb', '--read-burst',        dest='read_burst',       metavar='N', type=int, help='number of IOs to dispatch as reads in one burst')
    group.add_argument('--wb', '--write-burst',       dest='write_burst',      metavar='N', type=int, help='number of IOs to dispatch as writes in one burst')
    group.add_argument('--rs', '--read-sleep-usec',   dest='read_sleep_usec',  metavar='N', type=int, help='sleep usec after read IO/burst')
    group.add_argument('--ws', '--write-sleep-usec',  dest='write_sleep_usec', metavar='N', type=int, help='sleep usec after write IO/burst')

    group = parser.add_argument_group('Configuration file')
    group.add_argument('-g','--generate-config', type=str, metavar='YAML', help='generates YAML config file')
    group.add_argument('-c','--config', dest='read_config', type=str, metavar='YAML', help='reads from config')

    group = parser.add_argument_group('Output')
    group.add_argument('-o','--output-csv', type=str, metavar='CSV', help='append to CSV file')
    group.add_argument('-y','--output-yaml', type=str, metavar='YAML', help='write a YAML file')

    args = parser.parse_args()

    if args.version:
        print(read_kio_version())
        sys.exit(0)

    if args.examples:
        program_name = os.path.basename(__file__)
        print(EXAMPLES.format(program_name))
        sys.exit(0)

    main(args)

