from pydoc import cli
import matplotlib.pyplot as plt

def readFile(fileName):
    fileObj = open(fileName, "r") #opens the file in read mode
    words = fileObj.read().splitlines() #puts the file into an array
    fileObj.close()
    return words

file = readFile("EDF10-5.txt")
i=0
values = []
# split latency and clients values
while (i<len(file)):
    values.append(file[i].split(", "))
    i=i+1


latency = []
clients = []
i=0
while (i<len(values)):
    latency.append(values[i][0])
    clients.append(values[i][1])
    i=i+1


plt.plot(clients, latency)
plt.xlabel('Clients')
plt.ylabel('Latency')
plt.title('Performance')
plt.show()
