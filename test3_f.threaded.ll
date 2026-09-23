source_filename = "test3_f.ll"

define i32 @f(i32 %x, i32 %y) {
entry:
  %c1 = icmp sgt i32 %x, 0
  br i1 %c1, label %then1, label %else1

then1:
  %a = add nsw i32 %y, 1
  br label %then2

else1:
  %b = add nsw i32 %y, -1
  br label %else2

then2:
  %m = mul nsw i32 %a, 2
  br label %join

else2:
  %s = sub nsw i32 %b, 5
  br label %join

join:
  %r = phi i32 [ %m, %then2 ], [ %s, %else2 ]
  ret i32 %r
}
