#!/usr/bin/python
import sys
import subprocess
import requests
from pyfzf.pyfzf import FzfPrompt

def usage():
    print("Usage: cplayer url.txt")

def read(fname):
    f = open(fname,"r")
    url = f.readline()[:-1]
    f.close()

    return url

def get_content(url):
    r = requests.get(url)
    assert(r.ok)
    return r.text

def parse(raw_playlist):
    playlist = []
    title = ""
    url = ""
    get_url = False
    lines = raw_playlist.split("\n")
    for l in lines:
        if get_url:
            url = l[:-1]
            get_url = False
            playlist.append((title,url))
        elif "#EXTINF" in l:
            title = l.split(',')[-1][:-1]
            get_url = True
    return playlist

def get_url(title,playlist):
    for (t,u) in playlist:
        if t == title:
            return u
    return None

def launch_player(playlist):
    fzf = FzfPrompt()
    titles = []
    for (t,u) in playlist:
        titles.append(t)
    
    title = ""
    p = None
    while(True):
        if title != "": 
            title = ''.join(fzf.prompt(titles,"-q '"+title+"'"))
        else:
            title = ''.join(fzf.prompt(titles))
        
        url = get_url(title,playlist)
        if (p != None):
            p.kill()
            p.wait()
        p = subprocess.Popen(["mpv",url],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE)



def main():
    if (len(sys.argv) != 2):
        usage()
        exit(0)
    
    fname = sys.argv[1]
    
    print("Reading ...")
    url = read(fname)
    print("Getting content ...")
    raw_playlist = get_content(url)
    print("Parsing ...")
    playlist = parse(raw_playlist)
    print("Launching player ...")
    launch_player(playlist)

main()