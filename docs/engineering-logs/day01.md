https://medium.com/@mishraanaadi07/designing-a-fault-tolerant-edge-cluster-86e3c0a5dcfb

Engineering Log Day 1

This isn’t just a dissertation project.
I’m attempting to design and implement a lightweight distributed RTOS layer that allows embedded nodes to detect overload, cooperate, and recover from logical failure without depending on a central master.
That sentence sounds ambitious. It should.

The goal isn’t to build another dashboard or IoT demo. The goal is to design something that signals systems thinking, architectural discipline, and engineering maturity.

Today was Day 1. And Day 1 had very little to do with scheduling algorithms.
The Vision

The long-term idea is straightforward in words and complicated in reality:
* Multiple edge nodes running FreeRTOS
* Each capable of detecting overload
* Nodes aware of each other’s health
* Task delegation under stress
* No single point of failure

At a high level:
ESP Node A

ESP Node B

ESP Node C

↓

MQTT Broker

↓

Monitoring Dashboard

Each node publishes telemetry such as CPU usage, queue depth, and load factor. The dashboard exists purely for observability. It is not the brain.

That distinction matters.

If the dashboard becomes the coordinator, the system isn’t fault tolerant. It’s centralized with a pretty UI.
Why This Matters

Edge devices are increasingly expected to operate in unreliable environments. Most embedded systems are designed assuming stable execution and local control. What happens when we design them with distributed resilience in mind instead?
Reality: Toolchain Chaos

Before I could even think about overload detection, I had to fight something less glamorous: Toolchains.
I’m working on an Apple Silicon Mac.
Download the Medium app

ESP8266 RTOS SDK expects x86 toolchains. ESP32 uses a completely different SDK.
At first, I tried installing everything natively, It failed.
Wrong architecture. Wrong Python interpreter. Rosetta confusion. Xtensa compiler not found. Using the wrong Docker image for the wrong chip.
At one point I had successfully installed the wrong SDK inside the right container for the wrong processor.
That felt impressive in the wrong way.
The turning point was isolating the entire firmware build inside a Docker container forced to linux amd64. Once the toolchain lived in a predictable environment, everything stabilized.
This was a reminder that in embedded systems, environment stability is part of architecture.
Architectural Decision 1: Layer Isolation

Very early, I enforced a rule:
Only the network layer is allowed to touch ESP-specific APIs.
Everything else must use:
* Pure C
* FreeRTOS APIs only
* No SDK leakage into core logic
The project structure reflects that separation:
tasks | core | network

Why so strict?
Because, I am temporarily using ESP8266, but the final cluster will run on ESP32. If application logic depends on SDK-specific behavior, migration becomes painful.
By isolating the network layer, I preserved portability for:
* Scheduling logic
* Overload detection
* Future delegation mechanisms
That separation is not over-engineering. It is long-term risk reduction.
Architectural Decision 2: No Master Node

I considered introducing a cluster manager node.
It would have simplified coordination. It would also have introduced a single point of failure.
The goal is node-agnostic architecture.
If one node disappears, the system should degrade gracefully, not collapse.
That means future delegation will need negotiation logic instead of central assignment.
Harder? Yes.
More aligned with fault tolerance? Absolutely.
Observability Before Optimization

Before flashing real hardware, I built the monitoring path first:
* Local MQTT broker
* Flask-based dashboard
* Telemetry subscription to cluster plus wildcard telemetry
Then I simulated a node manually:

    mosquitto_pub -h localhost -t cluster/node1/telemetry -m ‘{“cpu”:30,”queue”:2,”load”:1000}’

The first time the dashboard updated from a manually published MQTT message, it felt more satisfying than I expected :) , No LEDs blinking. No motors spinning. Just structured data flowing through a system I designed.

The distributed pipeline works.
Even without physical hardware, I had:
Publisher | Broker | State aggregation | Frontend rendering
That full signal path matters more than blinking an LED.
Distributed systems without observability are just guesswork.
What Exists at the End of Day 1

By the end of today:
* Firmware builds reproducibly inside Docker
* Xtensa toolchain runs in a controlled environment
* MQTT broker runs locally
* Flask dashboard subscribes to cluster telemetry
* Nodes are dynamically registered in memory
* Load control channel is defined
No delegation yet. No overload migration yet.
But the foundation is stable.
And stability is not glamorous, but it is everything.

    An Unexpected Design Partner
    Throughout this process, I’ve been iterating heavily with an AI design partner.
    Not for copy-paste code generation.
    For architectural friction.
    Every time I proposed something naive, it forced me to justify it. Every time I drifted toward a trivial implementation, it pushed back with “why?”
    That forced clarity.
    It turns out explaining a system out loud, even to an AI, exposes weak thinking quickly.
    The value wasn’t automation. It was structured reasoning.

Lessons From Day 1

1. Environment setup is not operational overhead. It is architectural groundwork.
2. Layer isolation must be enforced early, not retrofitted later.
3. Observability should precede complexity.
4. Simulating distributed behavior before hardware arrival saves time.
5. Toolchain chaos is part of real engineering. Documenting it is part of maturity.

Current Status:
* Firmware Build: Stable in Docker
* Telemetry Pipeline: Operational
* Dashboard: Live and updating
* Delegation Engine: Not implemented
* Confidence Level: 6.5/10
What’s Next

Tomorrow:
* Flash ESP8266 with correct broker IP
* Validate live telemetry from real hardware
* Inject load dynamically
* Measure CPU and queue behavior
* Begin formalizing overload detection heuristics

Soon:
* Introduce heartbeat logic
* Track node liveness
* Define safe delegation contracts
* Simulate logical failure

This system is still small.
But it now has structure.
And structure is what separates a class project from infrastructure engineering.
Day 1 complete.
