@import "Host.vcl";

// Reads and writes host variables.
[Output("Out")]
float32 output = 0.0;

[NodeProcess]
void Process() {
    output = Host::Level * 2.0;
    Host::Meter = output;
}
