// A parameter is a compile-time value.
[Output("Out")]
float32 output = 0.0;

[Parameter("Factor"), Expose]
const float32 factor = 2.0;

[NodeProcess]
void Process() {
    output = 1.0;
}
