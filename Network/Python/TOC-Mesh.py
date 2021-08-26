#!/bin/python

import socket
import struct
import sys

class meshTool():

    message = socket.gethostname()
    multicast_addr = '224.3.29.71'
    port = 25566
    multicast_group = (multicast_addr, port)
    server_address = ('', port)

    # Dictate Size of neighborArray
    # I know there's better ways to initialise such and array, but this should be fine for a proof of concept
    w,h = 4,50
    neighborArray = [[0 for x in range(w)] for y in range(h)] 
    neighborCounter = 0


    # This is basic file containing all of the required functions to build out our Mesh Network.

    # This Listens out for MultiCast HELLO's from other devices, will log and respond upon recieving one.
    def meshListener(self):
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

        sock.bind(self.server_address)

        # Add socket to multicast group:
        group = socket.inet_aton(self.multicast_group)
        mreq = struct.pack('4sL', group, socket.INADDR_ANY)
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)

        # Return Hostname until listener is closed.
        while True:
            print >>sys.stderr, '\nwaiting to receive message'
            data, address = sock.recvfrom(1024)

            sock.sendto(socket.gethostname(), address)        

    def meshHello(self):
        # Create the datagram socket
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

        # Set a timeout so the socket does not block indefinitely when trying
        # to receive data.
        sock.settimeout(5)

        # Set the time-to-live for messages to 1 so they do not go past the
        # local network segment.
        ttl = struct.pack('b', 1)
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, ttl)

        try:

            # Send data to the multicast group
            print >>sys.stderr, 'sending "%s"' % self.message
            sent = sock.sendto(self.message, self.multicast_group)

            # Look for responses from all recipients
            while True:
                print >>sys.stderr, 'waiting to receive'
                try:
                    data, server = sock.recvfrom(16)
                except socket.timeout:
                    print >>sys.stderr, 'timed out, no more responses'
                    self.displayHosts()
                    break
                else:
                    print >>sys.stderr, 'received "%s" from %s' % (data, server)
                    self.addHost(data, server[0], server[1])
                    

        finally:
            print >>sys.stderr, 'closing socket'
            sock.close()

    def addHost(self,host,address,port):
        print('finish this later')
        #TODO: Build function mentioned above.
        # This Function should check an array, and add and remove entries based on the Hostnames that come back. This will be a prototpye so it shouldn't be that much of an issue.
        # this is also super bodge, but again, we can figure this out later
        self.neighborArray[0][self.neighborCounter] = host
        self.neighborArray[1][self.neighborCounter] = address
        self.neighborArray[2][self.neighborCounter] = port
        print('Added HOST: ' + self.neighborArray[0][self.neighborCounter] + ' IP: ' + self.neighborArray[1][self.neighborCounter] + ' PORT: ' + str(self.neighborArray[2][self.neighborCounter]))
        self.neighborCounter += 1
    
    def displayHosts(self):
        print('Current Neighbors: ')
        for i in range(self.neighborCounter):
            print('NEIGHBOR '+ str(i) +': NAME: ' + self.neighborArray[0][i] + ' IP: ' + self.neighborArray[1][i] + ' PORT: ' + str(self.neighborArray[2][i]))


    

# this is the Testing Part of the scripts
print('Hello World')

# Initialise the Mesh Class
mesh = meshTool()

# Send out the HELLO Message
mesh.meshHello()


# 