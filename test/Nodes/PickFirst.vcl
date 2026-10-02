// Both inputs follow what's connected, as Grog's Add does: B keeps its float initializer when only A
// is connected, to a block.
[AutoParameter]
using T = float32;

[Input("A")]
T a;
[Input("B")]
T b;

[Output("Out")]
T output;

[NodeProcess]
void Process() {
    output = a;
}
