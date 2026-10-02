// Each access once.
[Output("Out")]
float32 output = 0.0;

[Expose(Read, Read)]
float32 state = 0.0;

[NodeProcess]
void Process() {
    output = 1.0;
}
