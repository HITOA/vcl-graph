@import "Mix.vcl";

// Passes a struct declared here to a template of a cached library.
struct Pair {
    float32 a;
    float32 b;
}

[Output("Out")]
float32 output = 0.0;

[NodeProcess]
void Process() {
    Pair p;
    p.a = 1.5;
    p.b = 2.0;
    output = Mix::SumFields<Pair>(p);
}
