// An access is a bare identifier, not a string.
[Output("Out")]
float32 output = 0.0;

[Expose("Write")]
float32 state = 0.0;

[NodeProcess]
void Process() {
    output = 1.0;
}
