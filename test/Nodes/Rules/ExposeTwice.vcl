// One [Expose] states the whole access.
[Output("Out")]
float32 output = 0.0;

[Expose, Expose(Write)]
float32 state = 0.0;

[NodeProcess]
void Process() {
    output = 1.0;
}
