#!/usr/bin/env python3
import serial, sys, time
ser = serial.Serial('/dev/ttyACM1', 115200, timeout=1)
print("watching... press keys one at a time", flush=True)
last = None
t0 = time.time()
while True:
    b = ser.read(2)
    if len(b) == 2:
        p1, p3 = b[0], b[1]
        if (p1, p3) != last:
            last = (p1, p3)
            print(f"[{time.time()-t0:7.2f}s] P1={p1:08b}  P3={p3:06b}", flush=True)
