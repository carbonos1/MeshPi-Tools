#!/usr/bin/python

import socket


#get up host port
s = socket.socket()
host = socket.gethostname()
port = 12346
s.bind((host,port))

string = 'Thank you for connecting to TestNet'
# Build out a Basic Network Listener
s.listen(5)

conn, addr = s.accept()
# with conn:
while True:
    print(' Connected By', addr)
    conn.send(b'thank you for connecting to TestNet')
    conn.close()
