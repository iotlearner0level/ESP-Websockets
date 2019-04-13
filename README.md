# ESP-Websockets

In this experiment, we are able to create json string from data given by the ESP class and send the data via websoket to a webpage. The webpage is hosted by the esp and the browser loads it. subsequently, a websockets connection is established on port 81 of the esp which keeps on sending the json string to the web-browser. This approach is different from AJAX, where the browser is responsible for "pulling" the data from the esp server (polling). Everytime it retreives data, http header has to be sent increasing latency.

Websocket is much faster, and thanks to the websockets library by links2004, it is now possible on esp8266 within arduino itself. 

This code is compiled from various sources from internet.
