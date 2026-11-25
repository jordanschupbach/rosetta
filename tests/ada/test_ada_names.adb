--  The Ada API for tests/ada_full_names.hpp: names alike but for case or in Ada_Case, reserved
--  words, the runtime's and Standard's names, Native_Object_Record's operations, methods named like
--  types, a renamed virtual method, multiple inheritance, C++ hiding, operators, 64-bit values and in
--  out parameters. Argument: the generated names.ads.
with Ada.Command_Line;
with Ada.Containers;
with Ada.Directories;
with Ada.Exceptions; use Ada.Exceptions;
with Ada.Strings.Fixed;
with Ada.Strings.Unbounded; use Ada.Strings.Unbounded;
with Ada.Text_IO;
with Interfaces;
with Names; use Names;
with Ada_Names_Support; use Ada_Names_Support;

procedure Test_Ada_Names is
   use type Ada.Containers.Count_Type;
   use type Interfaces.Unsigned_64;

   Checks : Natural := 0;
   Failed : exception;

   procedure Check (Ok : Boolean; What : String) is
   begin
      Checks := Checks + 1;
      if not Ok then
         Ada.Text_IO.Put_Line (Ada.Text_IO.Standard_Error, "check failed: " & What);
         raise Failed;
      end if;
   end Check;

   procedure Test_Names is
      N : Native_Object_Access := New_Native_Object_Access;
      E : Native_Error_Access := New_Native_Error_Access;
      R : Rosetta_Call := New_Rosetta_Call;
      D : Data := New_Data;
      I : Integer_Access := New_Integer_Access;
      P : Point := New_Point;
      Q : Point_Access := New_Point_Access;
      C : Cursor := New_Cursor;
      Img : Image_Access := New_Image_Access;
      Cb : Callback_Access := New_Callback_Access;
   begin
      --  Classes named like the runtime's types, Standard's, or alike but for case.
      Check (N.Native = 2 and then E.What = "not an exception" and then R.Id = 3, "the runtime's names");
      Check (N.all in Native_Object_Record'Class and then Make_Integer (I) = 5, "Standard's names");
      Free (N);
      Free (E);
      Free (R);
      Free (I);
      Check (Img.Width = 6 and then Img.Image = "image" and then Cb.Calls = 7, "the runtime's subprograms and error kinds");
      Free (Img);
      Free (Cb);
      Check (D.Value = 4 and then Data_Functions.Twice (4) = 8, "a class before a policy module");
      Free (D);
      Check (Point_X (P) = 1 and then Point_Y (Q) = 2, "classes alike but for case");
      Free (P);
      Free (Q);
      Check (Rosetta_2 (2) = 6 and then The_Rosetta_Dispatch (2) = 8, "the runtime package's names");

      --  Reserved words take _Kw; names alike but for case take a number.
      Check (Begin_Kw (4) = 4 and then Repeat ("ab") = "abab" and then In_Kw (1, 2) = 3, "reserved words");
      Check (Length ("abc") = 30 and then Copy (1) = 2, "plain names");
      Check (Total (1) = 1 and then Total_2 (1, 2) = 3, "functions alike but for case");

      --  Enumerators.
      Check (Mode_Value (On) = 0 and then Mode_Value (On_Mode) = 1, "enumerators alike but for case");
      Check (Mode_Value (Self) = 2 and then Mode_Value (Type_Mode) = 3 and then Mode_Value (End_Mode) = 4 and then
             Mode_Value (Result) = 5 and then Mode_Value (Off) = 6 and then Mode_Value (Cursor_Mode) = 7,
             "reserved words and type names as enumerators");
      Check (Mode'Image (Type_Mode) = "TYPE_MODE", "enumerator names");

      --  Members.
      Check (C.Count = 20 and then C.Count_2 = 2, "a method before a field alike but for case");
      Check (C.Close_2 and then not C.Is_Closed and then C.Cast = "cast", "Native_Object_Record's operations first");
      Check (C.Image = "Cursor" and then C.Image_2 = "image" and then C.Finalize_2 = 11 and then C.Is_Closed_2 = 12,
             "Image, Finalize and Is_Closed");
      Check (C.Free_2 = 7 and then C.Destroy = 8 and then C.Result = 9 and then C.Class_Name = "cursor", "Free and plain names");
      Check (C.Get_Name = "camel" and then C.Get_Name_2 (2) = "snakesnake", "names alike in Ada_Case");
      Check (C.Label (3) = 6 and then C.Move (4) = 4 and then C.Position = 4 and then C.Equals (3), "parameters named Self");
      Check (C.Data_2 = 13 and then Cursor_Create = 5, "a method named like a type, a static function");
      C.Set_Count_2 (5);
      Check (C.Count = 50, "a renamed field setter");
      C.Close;
      Check (C.Is_Closed, "the generated Close");
      Free (C);
   end Test_Names;

   procedure Test_Classes is
      Res : Resource := New_Resource;
      F : File := New_File;
      Pipe : Resource := new Pipe_Record;
      T : Thing := New_Thing;
      N : Named;
      Shout : Named := new Shouting_Record;
      Loud : Thing := new Loud_Record;
      L : Leaf := New_Leaf;
      B : Base := New_Base;
      Deep : Leaf := new Deep_Record;
   begin
      --  A virtual method renamed in Ada: the override has the Ada name, and C++ calls it.
      Pipe.Construct_Resource;
      Check (Close_Resource (Res) = "C++ close" and then Close_Resource (F) = "file close", "renamed virtual method");
      Check (Close_Resource (Pipe) = "pipe C++ close" and then F.Close_2 = "file close", "Ada override of a renamed method");
      Free (Res);
      Free (F);
      Free (Pipe);

      --  Multiple inheritance: Counted is the parent type; Named's members are declared again.
      Check (T.all in Counted_Record'Class and then T.As_Named = T.As_Named, "both bases");
      Check (T.Counted_2 = 3 and then Counted_Of (T) = 3 and then T.Tag = "t", "the further base's members");
      Check (T.Label = "thing" and then Label_Of (T.As_Named) = "thing", "a further base argument");
      N := As_Named_2 (T);   --  the function avoids the member name (prefix notation would find both)
      Check (Label_Of (N) = "thing", "a pointer to the further base");
      Free (N);   --  a borrowed pointer: frees only the Ada object
      Free (T);   --  and the objects it owns
      Shout.Construct_Named;
      Loud.Construct_Thing;
      Check (Label_Of (Shout) = "NAMED" and then Label_Of (Loud.As_Named) = "loud thing", "Ada type extensions of either base");
      Free (Shout);
      Free (Loud);

      --  C++ hiding: Ada overloads Leaf's Scale with Base's; Leaf's Size takes another name.
      Check (L.Scale (2.0, 3.0) = 6.0 and then L.Scale (2.0) = 2.0, "overloads across types");
      Check (L.Size_2 = "big" and then L.Size = 1 and then B.Size = 1, "a method with another result takes its own name");
      Deep.Construct_Leaf;
      Check (Describe_Base (Deep) = "deep leaf" and then Describe_Base (L) = "leaf", "override two levels deep");
      Free (Deep);
      Free (B);
      Free (L);
   end Test_Classes;

   procedure Test_Values is
      M : Money := New_Money (250);
      Other : Money := New_Money (5);
      R : Money;
      N : Integer := 1;
      Text : Unbounded_String := To_Unbounded_String ("a");
      Pairs : String_Integer_Pair_Vector;
      T : Integer_String_Long_Float_Tuple;
   begin
      --  Operators, Image and Call.
      Check (M.Image = "2.50", "to_string is Image");
      R := M + Other;
      Check (R.Cents = 255, "operator");
      Free (R);
      R := M * 2;
      Check (R.Cents = 500, "operator with a scalar");
      Free (R);
      Check (not (M < Other) and then Other < M, "comparison");
      R := New_Money;
      Check (M.Call (5) = 255 and then (not R) and then not (not M = True), "prefix operator and Call");
      Free (R);
      Free (Other);
      Free (M);
      declare
         P : Padded := New_Padded (7);
      begin
         Check (P.Image = "007" and then Native_Object (P).Image = "007" and then P.Image (5) = "00007",
                "to_string with default arguments is Image");
         Free (P);
      end;

      --  Values.
      Check (Biggest = Interfaces.Unsigned_64'Last and then Echo_U64 (Interfaces.Unsigned_64'Last) = Interfaces.Unsigned_64'Last,
             "unsigned 64-bit values");
      Check (Echo_I64 (Long_Long_Integer'First) = Long_Long_Integer'First, "signed 64-bit minimum");
      Check (not Negate (True) and then Negate (False), "Booleans");
      Check (Kind_Of (True) = "bool" and then Kind_Of (Long_Long_Integer'(5)) = "int" and then Kind_Of ("5") = "string",
             "Booleans, numbers and strings stay apart");
      Bump (N, Text);
      Check (N = 2 and then Text = "a!", "in out parameters written back");
      Check (Call_Twice (Inc1'Access, 1) = 3, "subprogram");
      begin
         Check (Call_Twice (Failing'Access, 1) = 0, "unreachable");
         Check (False, "subprogram error");
      exception
         when E : Binding_Error => Check (Exception_Message (E) = "from a subprogram", "a subprogram's exception comes back");
      end;
      Pairs := Ranked (["a", "b"]);
      Check (Pairs.Length = 2 and then Pairs (2).First = "b" and then Pairs (2).Second = 2, "vector of pairs");
      T := Triple;
      Check (T.Item_1 = 1 and then T.Item_2 = "two" and then T.Item_3 = 3.5, "tuple record");
   end Test_Values;
begin
   Check (Ada.Directories.Simple_Name (Ada.Command_Line.Argument (1)) = "names.ads", "generated spec path");
   Test_Names;
   Test_Classes;
   Test_Values;
   Ada.Text_IO.Put_Line ("Ada names passed (" & Ada.Strings.Fixed.Trim (Checks'Image, Ada.Strings.Left) & " checks)");
exception
   when Failed => Ada.Command_Line.Set_Exit_Status (1);
end Test_Ada_Names;
