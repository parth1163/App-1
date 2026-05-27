# App 1 scaffold — setup, blink, web monitor
Theme Banner-
  Theme- Industrial/Theme Park
  Rationale: I chose the Industrial/Theme-Park theme because it maps real-time 
  hardware status requirements like dispatch readiness and track clearance. Living in
  Florida and going to theme parks yearly, its interesting to know the hardware and 
  software side of things.

System Summary- 
This project sets up a basic industrial control loop for a 'Ride-X' dispatch system. 
It uses the ESP32’s dual-core architecture to keep safety= and network 
tasks separate to ensure stability and safety. Core 1 runs a dispatch status task that 
handles the ride's status (LOCKED/READY) by using a hardware-timer-based delay to ensure the blink 
rate never fluctuates. Core 0 handles the web server, which streams 
current ride status data as JSON to a browser dashboard. By making the safety beacon have 
its own core, I’ve made sure that heavy web traffic or dashboard refreshing can't cause 
the beacon's timing to flucutate, ensuring the system stays responsive for operators even during 
high traffic to maximize safety and reduce accidents.

Wokwi Link: https://wokwi.com/projects/465188348489134081

Concurrency Diagram-
[ ESP32 DUAL-CORE ]
           
  |------------------|     |------------------|
  |      CORE 0      |     |      CORE 1      |
  |                  |     |                  |
  |                  |     |                  |
  |  HTTP Web Server |     |  Dispatch Status |
  |  (Priority: 5)   |     |  (Priority: 5)   |
  |------------------|     |------------------|
                |                   |
(Reads State)   |                   | (Writes State)
                v                   v
     |------------------------------------|
     |    SHARED STATE (RAM)              |
     |    - volatile bool led_on          |
     |    - volatile uint32_t toggle_count|
     |------------------------------------|
                                    |
                                    | (Hardware Control)
                                    v
                                 ( LED )

Engineering Analysis Questions-
1. Why two tasks?
Based on previous Embedded Systems experience in a single super-loop, network processing 
inside poll_web_server() is inconsistent because spikes due to high traffic, can
block the entire CPU. This causes latency that delays the toggle_led() function
and the delay(1000) timing, causing the safety dispactch status to stutter, which violates 
the hard real-time constraints.

2. Pin defense
Running both tasks on a single core may cause CPU overload, especially under heavy 
web traffic. Pinning blink_task to Core 1 provides isolation, ensuring that network activity 
on Core 0 cannot steal CPU power/cycles or cause probleims regarding the safety dispatch operation.

3. Atomicity
a) From what i have read, on the Xtensa architecture, reading or writing a word-aligned bool is an atomic 
CPU instruction, meaning it completes in a single cycle and cannot be interrupted, making it safe to share 
between cores.
(b) I read online that replacing the bool with a struct makes the pattern unsafe because writing a structure 
takes multiple clock cycles; Core 0 could read the structure while Core 1 is halfway through the update, 
resulting errors.


AI Disclosure-
Tool: Gemini
Date Accessed: May 27, 2026

Application :
Provided code troubleshooting to resolve a compiler syntax error where I accidentally semicolon (;) instead of a comma in blink_task.

