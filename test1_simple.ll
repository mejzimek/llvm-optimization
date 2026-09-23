define i32 @test_simple(i32 %x) {
entry:
  %c1 = icmp ne i32 %x, 0
  br i1 %c1, label %outer, label %ret30

outer:
  %c2 = icmp ne i32 %x, 0
  br i1 %c2, label %ret10, label %ret20

ret10:
  br label %exit

ret20:
  br label %exit

ret30:
  br label %exit

exit:
  %r = phi i32 [ 10, %ret10 ], [ 20, %ret20 ], [ 30, %ret30 ]
  ret i32 %r
}
