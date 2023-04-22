## 2023 Fault Tolerant Computing HW2 ##
Implement the e-Voting server.

# Built With
* [ubuntu 2204]
* [cmake v3.22.1]
* [Docker v20.10.17]

# prerequire
* install grpc (https://grpc.io/docs/languages/cpp/quickstart/)
* install cmake [sudo apt install -y cmake]
* install libsodium (https://doc.libsodium.org/installation)
* install docker & docker-compose
* install libpqxx [sudo apt install libpqxx-dev]

# change the server port
default address and port is 0.0.0.0:50051
you can change the port by running
./eVoting_server [port] 
./eVoting_client [address:port]

# change the database server setting
The database is postgressql system, built by docker

default database address and port is 0.0.0.0:8001
you can change the port and setting
1. In docker-compose.yml change port 8001 to other port
    ports:
      - "8001:5432"

2. change the connection setting in eVoting_server.cc line:38
    #define DBCONNECTINFO "dbname=eVoting user=admin password=passwd hostaddr=127.0.0.1 port=8001"

# compile and run
1. In ./cmake/common.cmake Line:51 "$ENV{HOME}/grpc" change to your grpc folder if your grpc folder is in other directory

2. compile : In the root dirctory run
```sh
    cmake -B .
    make
```

3. start database in docker
```sh
    sudo docker-compose up -d
```

4. run server (default address and port is 0.0.0.0:50051)
./eVoting_server [port] 
**After run ./eVoting_server, run "init" to initialize the database if it is first times run in this computer**

5. run cilent (default address and port is 0.0.0.0:50051)
./eVoting_client [address:port]


# note
In this version, RegisterVoter will generate key pair which will save in key/ folder.
and the file name is "${voter name}pk" for pubilc key, "${voter name}sk" for secret key.

you can move the secret key file, but don't change pubilc key file.
client login need to read the secret key file, and the server will read pubilc key file.

the step may occur some error and let the RegisterVoter may generate a Voter which can not login.
need to UnregisterVoter and RegisterVoter again.