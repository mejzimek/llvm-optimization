source_filename = "test1_simple.ll"

define i32 @test_simple(i32 %x) {
entry:
  %c1 = icmp ne i32 %x, 0
  br i1 %c1, label %outer, label %ret30

outer:
  %c2 = icmp ne i32 %x, 0
  br label %ret10

ret10:
  br label %exit

ret30:
  br label %exit

exit:
  %r = phi i32 [ 10, %ret10 ], [ 30, %ret30 ]
  ret i32 %r
}
