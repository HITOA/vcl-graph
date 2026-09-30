// Writes its output on every other call only: the output must persist.
[Output("Out")]
float32 output = 0.0;

float32 calls = 0.0;
uint32 parity = 0;

[NodeProcess]
void Process() {
    if (parity == 0)
        output = calls;
    parity = 1 - parity;
    calls += 1.0;
}
