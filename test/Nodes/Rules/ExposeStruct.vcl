// Only variables are exposed.
[Output("Out")]
float32 output = 0.0;

[Expose]
struct Pair {
    float32 a;
    float32 b;
}

[NodeProcess]
void Process() {
    output = 1.0;
}
