// What a UI may watch: an input, an output written on every call, state; and state it may edit.
[Input("In"), Expose]
float32 input = 0.0;

[Output("Out"), Expose]
float32 output;

[Expose]
uint32 calls = 0;

[Expose]
Vec<float32> last;

[Expose(Write)]
float32 offset = 0.0;

[NodeProcess]
void Process() {
    calls += 1;
    output = input + offset;
    last = output;
}
