source_filename = "test2_medium.ll"

define i32 @test_medium(i32 %x, i32 %y) {
entry:
  %c1 = icmp sgt i32 %x, 10
  br i1 %c1, label %then, label %else

then:
  %sum = add nsw i32 %x, %y
  %c2 = icmp sgt i32 %x, 10
  br label %inner.then

inner.then:
  %mul = mul nsw i32 %sum, 2
  br label %inner.end

inner.end:
  br label %exit

else:
  %diff = sub nsw i32 %x, %y
  br label %exit

exit:
  %r = phi i32 [ %mul, %inner.end ], [ %diff, %else ]
  ret i32 %r
}
