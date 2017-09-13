import buggy
import time

def echo(message):
    print(list(message))

def drive(message):
    if len(message) == 3:
        if message[0] == 0:
            direction = ((message[2] << 8) | message[1]) - 1500
            if direction >= -500 and direction <= 500:
                buggy.setDirection(direction)
        elif message[0] == 1:
            speed = ((message[2] << 8) | message[1]) - 1500
            if speed >= -500 and speed <= 500:
                buggy.setSpeed(speed)
        elif message[0] == 2:
            pan = ((message[2] << 8) | message[1]) - 1500
            if pan >= -500 and pan <= 500:
                buggy.setPan(pan)
        elif message[0] == 3:
            tilt = ((message[2] << 8) | message[1]) - 1500
            if tilt >= -500 and tilt <= 500:
                buggy.setTilt(tilt)

#buggy.addMessageListener(echo)
buggy.addMessageListener(drive)

while True:
    time.sleep(1)
