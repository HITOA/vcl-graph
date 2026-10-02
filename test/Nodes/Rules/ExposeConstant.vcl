// A constant has no run-time storage.
[Output("Out")]
float32 output = 0.0;

[Expose]
const float32 scale = 2.0;

[NodeProcess]
void Process() {
    output = 1.0;
}
