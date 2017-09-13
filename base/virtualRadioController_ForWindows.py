import base
import time
import threading
import curses
import copy

#Set de default parameter to have the car not moving and the tires straight 
defaults = { 
    'direction': 1460,
    'speed': 1548,
    'control': 2,
}

state = {
    'direction': defaults['direction'],
    'speed': defaults['speed'],
    'control': defaults['control'],
    'target-control': None
}

def telemetryCallback(message):
    state['control'] = message[0]
base.addMessageListener(telemetryCallback)

def telemetry():
    while True:
        base.requestTelemetryDump()
        time.sleep(0.1)
telemetryThread = threading.Thread(target = telemetry)
telemetryThread.daemon = True
telemetryThread.start()

screen = curses.initscr()
curses.start_color()
curses.use_default_colors()
curses.init_pair(1, curses.COLOR_GREEN, -1)
curses.init_pair(2, curses.COLOR_YELLOW, -1)
curses.init_pair(3, curses.COLOR_RED, -1)
curses.init_pair(4, -1, -1)
curses.noecho()
curses.cbreak()
screen.keypad(True)

def display():
    while True:
        screen.clear()
        screen.addstr(2, 0, 'arrows: change direction and speed', curses.color_pair(4))
        screen.addstr(3, 0, 'shift + left, shift + right: change direction faster', curses.color_pair(4))
        screen.addstr(4, 0, 's: switch control modes', curses.color_pair(4))
        screen.addstr(5, 0, 'r: reset direction and speed', curses.color_pair(4))
        screen.addstr(6, 0, 'q: quit', curses.color_pair(4))
        screen.addstr(
            0,
            0,
            'Direction: {:>4} | Speed: {:>4} | Control: '.format(state['direction'], state['speed']),
            curses.color_pair(4)
        )
        control = copy.copy(state['control'])
        if control == 0:
            screen.addstr('base ', curses.color_pair(1))
        elif control == 1:
            screen.addstr('radio', curses.color_pair(2))
        else:
            screen.addstr('lost ', curses.color_pair(3))
        screen.refresh()
        base.sendBytes(bytearray((0, state['direction'] & 0xff, state['direction'] >> 8)))
        base.sendBytes(bytearray((1, state['speed'] & 0xff, state['speed'] >> 8)))
        targetControl = copy.copy(state['target-control'])
        if not targetControl is None:
            if targetControl == state['control']:
                state['target-control'] = None
            else:
                if targetControl == 0:
                    base.switchToBaseControl()
                elif targetControl == 1:
                    base.switchToRadioControl()
displayThread = threading.Thread(target = display)
displayThread.daemon = True
displayThread.start()

while True:
    character = screen.getch() #Allow to recover the information send by the user through the wireless communication

#The next code can be modify by the user to implement his own answer to the data send through the wireless communication

    if character == ord('q'):
        break
    elif character == ord('r'):
        state['direction'] = defaults['direction']
        state['speed'] = defaults['speed']
    elif character == ord('s'):
        control = copy.copy(state['control'])
        if control == 0:
            state['target-control'] = 1
        elif control == 1:
            state['target-control'] = 0
    elif character == curses.KEY_RIGHT:
        if state['direction'] < 2000:
            state['direction'] += 1
    elif character == curses.KEY_LEFT:
        if state['direction'] > 1000:
            state['direction'] -= 1
    elif character == curses.KEY_UP:
        if state['speed'] < 2000:
            state['speed'] += 1
    elif character == curses.KEY_DOWN:
        if state['speed'] > 1000:
            state['speed'] -= 1
    elif character == curses.KEY_SRIGHT:
        if state['direction'] < 1991:
            state['direction'] += 10
        else:
            state['direction'] = 2000
    elif character == curses.KEY_SLEFT:
        if state['direction'] > 1009:
            state['direction'] -= 10
        else:
            state['direction'] = 1000

# End of data user modification

curses.nocbreak();
screen.keypad(False);
curses.echo()
curses.endwin()
