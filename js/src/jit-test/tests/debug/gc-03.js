// |jit-test| debug
// Storing a property on a Debug.Object protects it from GC as long as the
// referent is alive.

var g = newGlobal('new-compartment');
var N = g.N = 3;
var dbg = Debug(g);

var i = 0;
dbg.hooks = {
    debuggerHandler: function (frame) {
        frame.arguments[0].id = i++;
    }
};
g.eval("function f(x) { debugger; }");
g.eval("var arr = [], j; for (j = 0; j < N; j++) arr[j] = {};");
g.eval("for (j = 0; j < N; j++) f(arr[j]);");
assertEq(i, N);

gc(); gc();

i = 0;
dbg.hooks.debuggerHandler = function (frame) {
    assertEq(frame.arguments[0].id, i++)
}
g.eval("for (j = 0; j < N; j++) f(arr[j]);");
assertEq(i, N);
