# XInput-Poller
## A simple Multithreaded poller for xinput


When fetching XInput state in the main thread, many in-between state are getting lost, because in 16 Miliseconds a lot can happen. So to do xinput porpper you need a input poller that operates in a seperate thread. 
I couldn't a find decent XInput poller on the internet so I made one myself.

## dependencies 
https://github.com/mattiasgustavsson/libs/blob/main/thread.h
