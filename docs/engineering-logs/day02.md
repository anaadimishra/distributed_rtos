
MQTT server not working as expected:

1. I see esp hs connected to router, and the dashboard app is running perfectly, still no connection estabished with the broker!
2. Is node publishing anything? No! can't see any packets! hmm!
3. Broker IP configured in esp? Check, Port? Check. 




[//]: # "bc:113"

Perfect. This is exactly the kind of issue worth documenting.

Here’s a clean, publishable draft you can drop into:

```
docs/engineering-log/day02.md
```

or adapt into your Medium post.

---

# Debugging MQTT Connectivity: The “Local Only Mode” Trap

After successfully flashing the ESP8266 and confirming WiFi connectivity, I expected telemetry to appear instantly on the dashboard.

It didn’t.

The ESP was connected to WiFi.
The dashboard was running.
The MQTT broker was running.

Yet no telemetry appeared.

At first glance, this looked like a firmware issue. It wasn’t.

---

## Step 1: Verifying the Broker

I checked whether Mosquitto was listening:

```bash
lsof -i :1883
```

The output showed:

```
mosquitto ... TCP localhost:1883 (LISTEN)
```

That line was subtle but critical.

The broker was listening only on `localhost`.

This meant:

* The dashboard (running on the same Mac) could connect
* The ESP (on the local WiFi network) could not

The system was technically operational, but isolated.

---

## The Real Cause: Mosquitto 2.x Default Behavior

Mosquitto 2.x introduced a secure-by-default configuration.

When started without a config file:

```bash
mosquitto -p 1883 -v
```

It runs in:

```
Starting in local only mode.
Connections will only be possible from clients running on this machine.
```

This prevents remote connections unless explicitly allowed.

Good for production safety.
Confusing for development.

---

## The Fix

Instead of relying on defaults, I created a minimal configuration file:

`mosquitto.conf`

```
listener 1883 0.0.0.0
allow_anonymous true
```

Then started the broker using:

```bash
mosquitto -c mosquitto.conf -v
```

Now `lsof` showed:

```
TCP *:1883 (LISTEN)
```

This confirmed that the broker was listening on all network interfaces.

---

## Result

After restarting the ESP:

```
New connection from 192.168.1.xxx on port 1883.
```

Telemetry immediately appeared on the dashboard.

The system pipeline was finally complete:

ESP → MQTT Broker → Flask Dashboard → Browser

---

## Lessons Learned

1. Always verify which interface a service is bound to.
2. “It’s running” does not mean “it’s reachable.”
3. Security defaults can silently block distributed systems.
4. Observability tools like `lsof` are invaluable in debugging network issues.

---

## Why This Matters

This issue had nothing to do with FreeRTOS or embedded logic.

It was purely a networking boundary problem.

But in distributed systems, boundaries are everything.

A system can be perfectly correct internally and still fail because of configuration at the edges.

Understanding those boundaries is part of building resilient systems.

---
