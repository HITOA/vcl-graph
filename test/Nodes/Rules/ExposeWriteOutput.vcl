// The node owns its outputs: the UI may read one, not write it.
[Output("Out"), Expose(Write)]
float32 output = 0.0;

[NodeProcess]
void Process() {
    output = 1.0;
}
