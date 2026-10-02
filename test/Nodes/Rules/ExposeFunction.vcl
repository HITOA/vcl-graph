// Only variables are exposed.
[Output("Out")]
float32 output = 0.0;

[Expose]
float32 Half(float32 x) {
    return x * 0.5;
}

[NodeProcess]
void Process() {
    output = 1.0;
}
