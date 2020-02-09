#!/usr/bin/python3
import urllib.request
import tarfile
import threading
import os
import subprocess

def run_shell(shell_params, input=None, print_command=True):
    if print_command:
        print("> %s" % (shell_params))
    r = subprocess.Popen(shell_params, universal_newlines=True, shell=True, stdout=subprocess.PIPE,
                         stdin=subprocess.PIPE).communicate(input=input)
    if r[1] != None:
        raise Exception(r[1])
    return r[0]

boost_url = "https://dl.bintray.com/boostorg/release/1.72.0/source/boost_1_72_0.tar.bz2"
ffmpeg_url = "https://ffmpeg.org/releases/ffmpeg-4.2.2.tar.bz2"
glog_url="https://github.com/google/glog/archive/v0.4.0.tar.gz"
gtest_url = "https://github.com/google/googletest/archive/release-1.10.0.tar.gz"

urls = [
    {"url": boost_url, "name": "boost"}, 
    {"url": ffmpeg_url, "name": "ffmpeg"}, 
    {"url": glog_url, "name": "glog"},
    {"url": gtest_url, "name": "gtest"}
]

base_path = "./temp/"

def download(fileurl, name):
    path = base_path + name + ".tar.gz"
    urllib.request.urlretrieve(fileurl, path)
    print("download " + fileurl + " ok")
    tar = tarfile.open(path)
    tar.extractall(path=base_path + name + "/")
    tar.close()
    list = os.listdir(base_path + name + "/")
    print(list)
    path = base_path + name + "/" + list[0] + "/"
    run_shell("cmake " + path + " & make -j "+ path + " & make " + "  install " + path)
    
    

def main():
    threads = []
    if os.path.exists(base_path) == False:
        os.mkdir(base_path)
    for i in urls:
        thread = threading.Thread(target = download, args=(i["url"], i["name"]))
        thread.start();    
        threads.append(thread)
    for i in threads:
        i.join()

main()