define i32 @test_equivalent(i32 %x, i32 %y) {
entry:
  %c1 = icmp sgt i32 %x, 10
  br i1 %c1, label %then, label %else

then:
  %sum = add nsw i32 %x, %y
  %c2 = icmp sge i32 %x, 11
  br i1 %c2, label %inner.then, label %inner.else

inner.then:
  %mul = mul nsw i32 %sum, 2
  br label %inner.end

inner.else:
  %dec = sub nsw i32 %sum, 2
  br label %inner.end

inner.end:
  %inner = phi i32 [ %mul, %inner.then ], [ %dec, %inner.else ]
  br label %exit

else:
  %diff = sub nsw i32 %x, %y
  br label %exit

exit:
  %r = phi i32 [ %inner, %inner.end ], [ %diff, %else ]
  ret i32 %r
}
