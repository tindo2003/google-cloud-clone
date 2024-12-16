#!/bin/bash

gnome-terminal --tab -- bash -c "cd /home/cis5050/Desktop/fa24-cis5050-T07/backend && make clean && make coordinator && ./coordinator config 8006 9000; exec bash"
sleep 1

gnome-terminal --tab -- bash -c "cd /home/cis5050/Desktop/fa24-cis5050-T07/backend && make multiserver && ./multiserver -p 8006 config 1; exec bash"
sleep 1

gnome-terminal --tab -- bash -c "cd /home/cis5050/Desktop/fa24-cis5050-T07/backend && make multiserver && ./multiserver -p 8006 config 2; exec bash"
sleep 1

gnome-terminal --tab -- bash -c "cd /home/cis5050/Desktop/fa24-cis5050-T07/backend && make multiserver && ./multiserver -p 8006 config 3; exec bash"
sleep 1

gnome-terminal --tab -- bash -c "cd /home/cis5050/Desktop/fa24-cis5050-T07/http && make clean && make load_balancer && ./load_balancer; exec bash"
sleep 1
gnome-terminal --tab -- bash -c "cd /home/cis5050/Desktop/fa24-cis5050-T07/http && make server && ./server 1; exec bash"
sleep 1
gnome-terminal --tab -- bash -c "cd /home/cis5050/Desktop/fa24-cis5050-T07/http && make server && ./server 3; exec bash"