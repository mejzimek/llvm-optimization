define i32 @f(i32 %x, i32 %y) {
entry:
  %c1 = icmp sgt i32 %x, 0
  br i1 %c1, label %then1, label %else1

then1:
  %a = add nsw i32 %y, 1
  br label %mid

else1:
  %b = add nsw i32 %y, -1
  br label %mid

mid:
  %p = phi i32 [ %a, %then1 ], [ %b, %else1 ]
  %c2 = icmp sgt i32 %x, 0
  br i1 %c2, label %then2, label %else2

then2:
  %m = mul nsw i32 %p, 2
  br label %join

else2:
  %s = sub nsw i32 %p, 5
  br label %join

join:
  %r = phi i32 [ %m, %then2 ], [ %s, %else2 ]
  ret i32 %r
}
