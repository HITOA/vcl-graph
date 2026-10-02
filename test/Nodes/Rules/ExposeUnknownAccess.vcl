// [Expose] takes the accesses Read and Write only.
[Output("Out")]
float32 output = 0.0;

[Expose(Maybe)]
float32 state = 0.0;

[NodeProcess]
void Process() {
    output = 1.0;
}
