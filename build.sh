#!/bin/bash
avr-g++ -mmcu=attiny85 -Os -Wall -DF_CPU=8000000L -I. -c main.cpp -o AdjacentReality.o
