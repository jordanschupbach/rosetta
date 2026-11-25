--  Ada API (--language ada) on the library-shaped example shared with the Python, Lua, Common Lisp,
--  Julia, C#, VB, Go, Rust, PHP, Perl, Swift, Pascal and Dart backends. Argument: the generated
--  features.ads, for the documentation checks.
with Ada.Command_Line;
with Ada.Containers.Indefinite_Vectors;
with Ada.Exceptions; use Ada.Exceptions;
with Ada.Strings.Fixed;
with Ada.Strings.Unbounded; use Ada.Strings.Unbounded;
with Ada.Text_IO;
with Interfaces;
with Features; use Features;
with Ada_Full_Support; use Ada_Full_Support;

procedure Test_Ada_Full is
   use type Ada.Containers.Count_Type;
   use type Interfaces.Unsigned_8;

   Checks : Natural := 0;
   Last_Kind : Native_Error_Kind := Not_Native;
   Last_Id : Exception_Id := Null_Id;
   Failed : exception;

   package Line_Vectors is new Ada.Containers.Indefinite_Vectors (Positive, String);
   Source : Line_Vectors.Vector;

   procedure Check (Ok : Boolean; What : String) is
   begin
      Checks := Checks + 1;
      if not Ok then
         Ada.Text_IO.Put_Line (Ada.Text_IO.Standard_Error, "check failed: " & What);
         raise Failed;
      end if;
   end Check;

   function Has (Text, Part : String) return Boolean is (Ada.Strings.Fixed.Index (Text, Part) > 0);

   --  An exception the code before raised: it must mention Pattern.
   procedure Got (E : Exception_Occurrence; Pattern : String) is
   begin
      Checks := Checks + 1;
      if not Has (Exception_Message (E), Pattern) then
         Ada.Text_IO.Put_Line (Ada.Text_IO.Standard_Error, "expected an error containing " & Pattern & ", got: " &
                               Exception_Name (E) & ": " & Exception_Message (E));
         raise Failed;
      end if;
      Last_Kind := Error_Kind (E);
      Last_Id := Exception_Identity (E);
   end Got;

   procedure Missed (Pattern : String) with No_Return is
   begin
      Ada.Text_IO.Put_Line (Ada.Text_IO.Standard_Error, "expected an error containing " & Pattern & ", got none");
      raise Failed;
   end Missed;

   --  The comment lines right below the first line containing Declaration.
   function Doc (Declaration : String) return String is
      Result : Unbounded_String;
   begin
      for I in Source.First_Index .. Source.Last_Index loop
         declare
            Line : constant String := Ada.Strings.Fixed.Trim (Source (I), Ada.Strings.Both);
         begin
            if Has (Line, Declaration) and then not (Line'Length >= 2 and then Line (Line'First .. Line'First + 1) = "--") then
               for J in I + 1 .. Source.Last_Index loop
                  declare
                     Next : constant String := Ada.Strings.Fixed.Trim (Source (J), Ada.Strings.Both);
                  begin
                     exit when Next'Length < 2 or else Next (Next'First .. Next'First + 1) /= "--";
                     Append (Result, Ada.Strings.Fixed.Trim (Next (Next'First + 2 .. Next'Last), Ada.Strings.Both) & ASCII.LF);
                  end;
               end loop;
               return To_String (Result);
            end if;
         end;
      end loop;
      Ada.Text_IO.Put_Line (Ada.Text_IO.Standard_Error, "not declared: " & Declaration);
      raise Failed;
   end Doc;

   function Whole_Source return String is
      Result : Unbounded_String;
   begin
      for Line of Source loop
         Append (Result, Line & ASCII.LF);
      end loop;
      return To_String (Result);
   end Whole_Source;

   ------------------------------------------------------------------ checks
   procedure Test_Classes is
      C1 : Circle := New_Circle (2.0);
      S : Square := New_Square;
      Made, Shape_1, Shared : Shape;
      Transferred : Circle;
      Canvas_1 : Canvas := New_Canvas;
   begin
      Check (C1.Area = 12.0 and then C1.Name = "circle" and then C1.Twice_Area = 24.0 and then C1.Radius = 2.0, "circle methods");
      Check (C1.Image = "Circle", "Image names the class");
      Check (C1.all in Shape_Record'Class, "class hierarchy");
      begin
         Transferred := New_Circle (-1.0);
         Missed ("negative radius");
      exception
         when E : Invalid_Argument_Error => Got (E, "negative radius");
      end;
      Check (Last_Kind = Invalid_Argument, "exception kind");
      Check (S.Side = 1.0 and then S.Area = 1.0, "default constructor");
      S.Set_Side (3.0);
      Check (S.Area = 9.0, "field set");
      declare
         S2 : Square := New_Square (2.0);
      begin
         Check (S2.Area = 4.0, "overloaded constructor");
         Free (S2);
         Check (S2 = null, "Free sets null");
      end;
      Check (Area_Of (S) = 9.0 and then Describe (C1) = "circle:0", "functions on classes");
      --  Enumerations.
      Check (C1.Kind_2 = Circle_Kind and then S.Kind_2 = Square_Kind, "enum results");
      Check (Filled (Solid) and then not Filled (None), "enum arguments");
      Check (Kind'Image (Custom) = "CUSTOM" and then Custom'Enum_Rep = 7, "enum values");
      Check (Shape_Sides_Unknown = -1 and then abs (Golden - 1.618) < 1.0E-12, "constants");

      --  Most-derived results, ownership transfer and shared ownership.
      Made := Make_Shape (Circle_Kind, 1.0);
      Check (Made.all in Circle_Record'Class and then Circle (Made).Radius = 1.0, "most-derived result");
      Free (Made);
      Shape_1 := Make_Shape (Square_Kind, 2.0);
      Canvas_1.Add (Shape_1);
      Free (Shape_1);   --  C++ took the object: the Ada object is closed
      Shape_1 := Shared_Square (3.0);
      Canvas_1.Share (Shape_1);
      Free (Shape_1);
      Transferred := New_Circle (1.0);
      Canvas_1.Add (Transferred);
      begin
         Check (Transferred.Area = 0.0, "unreachable");
         Missed ("closed");
      exception
         when E : Binding_Error => Got (E, "closed");
      end;
      Free (Transferred);
      Check (Canvas_1.Size = 3 and then Canvas_1.Total = 4.0 + 9.0 + 3.0, "ownership transfer");
      Check (Canvas_1.At_Kw (0).all in Square_Record'Class and then Canvas_1.At_Kw (0).Area = 4.0, "borrowed element");
      Shared := Shared_Square (2.0);
      Canvas_1.Share (Shared);
      Check (Shared.Area = 4.0 and then Canvas_1.Total = 20.0, "shared ownership");
      Transferred := New_Circle (1.0);
      Check (Total_Area ([Shape (Transferred), Shared]) = 7.0, "vector of objects");
      Free (Transferred);
      Free (Shared);
      Check (Canvas_1.Total = 20.0, "C++ keeps a shared object after Ada frees it");
      Free (Canvas_1);
      Free (S);
      Free (C1);
   end Test_Classes;

   Thread_Result : Long_Float := 0.0;

   task type Caller;
   task body Caller is
   begin
      Thread_Result := Apply (Plus_One'Access, 1.0);
   end Caller;

   procedure Test_Values is
      Words : String_To_Integer_Map;
      Pair : Long_Float_Long_Float_Pair;
      Tr : Long_Float_Vector_Vector;
      Cs : Circle_Vector;
      Values : Long_Float_Vector;
      Bytes : Unsigned_8_Vector;
      C : Circle;
      Plus : Integer_To_Integer_Function;
   begin
      --  Overloads: Ada overloads, resolved again in C++.
      Check (Pick (1) = "int" and then Pick (1.5) = "double" and then Pick (2.0) = "double" and then Pick ("x") = "string", "scalar overloads");
      C := New_Circle (1.0);
      Check (Pick (Long_Float_Vector'[1.0, 2.0]) = "vector" and then Pick (C) = "shape", "container and class overloads");
      Free (C);
      Check (Add (1) = 111 and then Add (1, 2) = 103 and then Add (1, 2, 3) = 6, "default arguments");

      --  Containers.
      Words := Count_Words (["a", "b", "a"]);
      Check (Words.Length = 2 and then Words ("a") = 2 and then Words ("b") = 1, "map result");
      Pair := Min_Max ([3.0, 1.0, 2.0]);
      Check (Pair.First = 1.0 and then Pair.Second = 3.0, "pair result");
      begin
         Pair := Min_Max (Long_Float_Vectors.Empty_Vector);
         Missed ("empty input");
      exception
         when E : Out_Of_Range_Error => Got (E, "empty input");
      end;
      Check (Last_Kind = Out_Of_Range, "out_of_range kind");
      Check (Find ([5, 7], 7).Has_Value and then Find ([5, 7], 7).Value = 1 and then not Find ([5, 7], 9).Has_Value, "optional result");
      Tr := Transpose ([[1.0, 2.0, 3.0], [4.0, 5.0, 6.0]]);
      Check (Tr.Length = 3 and then Tr (1) (1) = 1.0 and then Tr (1) (2) = 4.0 and then Tr (3) (2) = 6.0, "nested vectors");
      Bytes := Bytes_Of ("AZ");
      Check (Byte_Sum ([1, 2, 250]) = 253 and then Bytes.Length = 2 and then Bytes (1) = 65 and then Bytes (2) = 90, "bytes");
      Cs := Circles (3);
      Check (Cs.Length = 3 and then Cs.Element (3).Radius = 3.0, "vector of class results");
      for I in Cs.First_Index .. Cs.Last_Index loop
         C := Cs (I);
         Free (C);
      end loop;
      Check (Repeat ('z', 3) = "zzz" and then Initial ("hello") = 'h', "characters");
      --  Non-const reference parameters are in out parameters, written back.
      Values := [1.0, 2.0];
      Scale (Values, 3.0);
      Check (Values.Length = 2 and then Values (1) = 3.0 and then Values (2) = 6.0, "in out argument written back");
      begin
         Check (Repeat ('z', -1) = "", "unreachable");
         Missed ("negative count");
      exception
         when E : Native_Error | Invalid_Argument_Error => Got (E, "negative count");
      end;
      Check (Defaults = "x*1-1.500000", "all defaults");
      Check (Defaults (Circle_Kind, "y", True) = "y*0!-1.500000", "some defaults");
      Check (Has (Doc ("function Defaults (The_Kind : Kind; Label : String; Loud : Boolean) return String"), "Weight := 0.5"),
             "omitted defaults named");

      --  Subprograms both ways.
      Check (Apply (Twice'Access, 21.0) = 42.0, "subprogram");
      begin
         Check (Apply (Failing'Access, 1.0) = 0.0, "unreachable");
         Missed ("from Ada");
      exception
         when E : others => Got (E, "from Ada");
      end;
      Check (Last_Id = Sentinel'Identity, "a subprogram's exception comes back unchanged");
      Plus := Adder (40);
      Check (Plus.Call (2) = 42, "C++ std::function as an Ada object");
      Free (Plus);
      Check (Apply (Via_Native'Access, 3.0) = 4.0, "C++ function inside a subprogram");
      Calls := 0;
      Check (Apply (Counting'Access, 5.0) = 5.0 and then Calls = 1, "subprogram with state");
      declare
         Worker : Caller;
      begin
         null;
      end;
      Check (Thread_Result = 2.0, "subprogram from another task");
   end Test_Values;

   procedure Test_Objects is
      V : Vec2 := New_Vec2 (1.0, 2.0);
      W : Vec2 := New_Vec2 (3.0, 4.0);
      R, Chained, Start_1 : Vec2;
      Seg : Segment;
      Ledger_1 : Ledger;
      Copy : Stamp;
      Aim_1 : Aim;
      Stack : Int_Stack;
      Items_1 : Integer_Vector;
      X : Circle;
      Tmp : Shape;
   begin
      --  Value type operators, chaining and borrowed references.
      R := V + W;
      Check (R.X = 4.0 and then R.Y = 6.0, "operator");
      Free (R);
      R := V * 2.0;
      Check (R.Y = 4.0, "multiply");
      Free (R);
      R := -V;
      Check (R.X = -1.0, "negate");
      Free (R);
      R := V - W;
      Check (R.X = -2.0, "subtract");
      Free (R);
      R := New_Vec2 (1.0, 2.0);
      Check (V.Equals (R) and then not V.Equals (W), "equality operator");
      Free (R);
      Check (V.Element (1) = 2.0, "index operator");
      R := New_Vec2 (1.0, 1.0);
      Check (V.Inner (R) = 3.0, "rename");
      Free (R);
      begin
         Check (V.Element (2) = 0.0, "unreachable");
         Missed ("Vec2 index");
      exception
         when E : others => Got (E, "Vec2 index");
      end;
      Chained := V.Set_X (10.0).Set_Y (20.0);
      Check (V.X = 10.0 and then V.Y = 20.0 and then Chained = V, "chaining returns the same Ada object");
      Check (V.Add_Assign (W) = V and then V.X = 13.0, "compound assignment");
      V.Set_X_2 (5.0);
      Check (V.X = 5.0, "field setter");
      R := V.Clone;
      Check (R.Y = 24.0, "C++ clone");
      Free (R);
      Check (Sum ([V, W]) = 5.0 + 24.0 + 3.0 + 4.0, "vector of values");
      Free (V);
      Free (W);

      Seg := New_Segment;
      W := New_Vec2 (3.0, 4.0);
      Seg.Set_End_Kw (W);
      Free (W);
      Check (Seg.Length2 = 25.0, "object field set");
      Start_1 := Seg.First;
      Start_1.Set_X_2 (3.0);
      Check (Seg.Start.X = 3.0 and then Seg.Length2 = 16.0, "borrowed reference writes through");
      Seg.Start.Set_Y_2 (4.0);
      Check (Seg.Length2 = 0.0, "object field borrowed");
      Check (Seg.Start = Seg.Start, "a member object is one Ada object");
      Free (Seg);   --  and the objects it owns

      --  Templates, extensions, qualified names and copy types.
      Stack := New_Int_Stack;
      Stack.Push (1);
      Stack.Push (2);
      Items_1 := Stack.Items;
      Check (Stack.Size = 2 and then Items_1.Length = 2 and then Items_1 (2) = 2 and then Stack.Pop = 2, "template instance");
      Free (Stack);
      Check (Clamp_Int (42) = 10 and then Clamp_Int (3, 5) = 3 and then Clamp_Real (2.5) = 2.5, "function templates");
      X := New_Circle (2.0);
      Check (X.Label = "#circle" and then X.Label ("@") = "@circle" and then X.Diameter = 4.0, "extensions");
      Free (X);
      X := Circle_Unit;
      Check (X.Radius = 1.0, "static extension");
      Free (X);
      declare
         Tag : Features_Legacy_Tag := New_Features_Legacy_Tag;
      begin
         Check (Features_Legacy_Version = 2 and then Tag.Id = 7, "qualified names");
         Free (Tag);
      end;
      Check (Features_Legacy_Mode'Image (Off) = "OFF", "qualified enum");
      Ledger_1 := New_Ledger;
      Copy := Ledger_1.Latest;
      Copy.Set_Value (5);
      Check (Ledger_1.Stamp_2.Value = 1, "copy type");
      Free (Copy);
      Free (Ledger_1);
      Aim_1 := New_Aim;
      Aim_1.Origin.Set_X_2 (2.0);
      Check (Aim_1.Target_2.Point.X = 2.0, "nested borrowed fields");
      Free (Aim_1);
      Check (Has (Whole_Source, "function Point (Self : Target_Record) return Vec2;") and then not Has (Whole_Source, "Set_Point"),
             "read-only field");

      --  Errors, closing and freeing.
      X := New_Circle (1.0);
      Check (X.Area = 3.0 and then not X.Is_Closed, "before close");
      X.Close;
      Check (X.Is_Closed and then X.Image = "Circle (closed)", "closed");
      begin
         Check (X.Area = 0.0, "unreachable");
         Missed ("closed");
      exception
         when E : Binding_Error => Got (E, "closed");
      end;
      Check (Last_Kind = Binding, "binding error kind");
      X.Close;
      Free (X);
      for I in 1 .. 200 loop
         Tmp := Make_Shape (Square_Kind, Long_Float (I));
         Check (Tmp.Area = Long_Float (I * I), "many objects");
         if I mod 2 = 0 then
            Tmp.Close;
         end if;
         Free (Tmp);
      end loop;
   end Test_Objects;

   procedure Test_Extensions is
      T : constant Triangle := new Triangle_Record'(Shape_Record with B => 4.0, H => 3.0);
      C, Q, Back : Shape;
      Canvas_1 : Canvas;
      U : Shape;
      Sm : constant Summer := new Summer_Record;
      V : Visitor;
   begin
      T.Construct_Shape;
      Check (T.Area = 6.0 and then Area_Of (T) = 6.0 and then T.Twice_Area = 12.0 and then Describe (T) = "triangle:7",
             "override called from C++ and from Ada");
      C := Shape (New_Circle (1.0));
      Q := Shape (New_Square (2.0));
      Check (Total_Area ([Shape (T), C, Q]) = 13.0 and then T.Label ("@") = "@triangle", "mixed overrides");
      Free (C);
      Free (Q);
      C := Shape (T);
      Free (C);
      C := new Incomplete_Record;
      C.Construct_Shape;
      begin
         Check (Area_Of (C) = 0.0, "unreachable");
         Missed ("pure virtual");
      exception
         when E : others => Got (E, "pure virtual");
      end;
      Free (C);
      C := new Failing_Record;
      C.Construct_Shape;
      begin
         Check (Area_Of (C) = 0.0, "unreachable");
         Missed ("from Ada");
      exception
         when E : others => Got (E, "from Ada");
      end;
      Free (C);
      Check (Last_Id = Sentinel'Identity, "an override's exception comes back unchanged");
      C := new Shape_Record;
      begin
         C.Construct_Shape;
         Missed ("abstract");
      exception
         when E : Binding_Error => Got (E, "abstract");
      end;
      Free (C);
      Check (Last_Kind = Binding, "constructing an abstract class");
      C := new Loud_Record;
      Circle_Record'Class (C.all).Construct_Circle (2.0);
      Check (Describe (C) = "CIRCLE:0" and then C.Name = "CIRCLE" and then C.Area = 12.0, "a view conversion runs the C++ method");
      Free (C);
      C := new Louder_Record;
      Circle_Record'Class (C.all).Construct_Circle (1.0);
      Check (Describe (C) = "circle!:0", "inherited override");
      Free (C);
      Canvas_1 := New_Canvas;
      U := new Unit_Shape_Record'(Shape_Record with Tag => To_Unbounded_String ("kept"));
      U.Construct_Shape;
      Canvas_1.Add (U);
      Check (U.Area = 1.0, "Ada object owned by C++");
      Q := Shape (New_Square (2.0));
      Canvas_1.Share (Q);
      Free (Q);
      Check (Canvas_1.Total = 5.0 and then Canvas_1.Size = 2, "C++ calls the Ada object it owns");
      Back := Canvas_1.At_Kw (0);
      Check (Back = U and then To_String (Unit_Shape_Record (Back.all).Tag) = "kept", "the same Ada object comes back");
      Canvas_1.Clear;
      Free (U);
      U := new Unit_Shape_Record;
      U.Construct_Shape;
      Canvas_1.Share (U);
      Check (Canvas_1.Total = 1.0, "shared Ada object");
      Canvas_1.Clear;
      Free (U);
      Free (Canvas_1);
      Sm.Construct_Visitor;
      Check (Run_Visitor (Sm, [1.0, 2.0]) = 30.0, "callback interface");
      Check (Sm.Seen.Length = 2 and then Sm.Seen (2) = 2.0, "extension state");
      Check (Sm.Visit (4.0) = 40.0, "a direct call runs the override");
      V := Visitor (Sm);
      Free (V);
      V := New_Visitor;
      Check (Run_Visitor (V, [1.0, 2.0]) = 3.0, "C++ default implementation");
      Free (V);
   end Test_Extensions;

   procedure Test_Docs is
      D : constant String := Doc ("function Add (A : Integer) return Integer;");
      U : Vec2;
   begin
      Check (Has (D, "Adds three integers, Socha & Dorigo style."), "summary");
      Check (Has (D, "@param A first term"), "parameters");
      Check (Has (D, "@return the sum"), "returns");
      Check (Has (D, "Example:") and then Has (D, "int total = add(1);"), "heading and code block");
      Check (Has (D, "See also: pick"), "see also");
      Check (Has (D, "@exception Out_Of_Range_Error (C++ std::out_of_range)"), "errors");
      Check (Has (D, "B := 10, C := 100"), "omitted defaults");
      Check (Has (Doc ("function New_Circle (Radius : Long_Float) return Circle;"), "Invalid_Argument_Error"), "constructor errors");
      Check (Has (Doc ("function X (Self : Vec2_Record) return Long_Float;"), "Horizontal component."), "field documentation");
      Check (Has (Doc ("type Shape_Record is new Native_Object_Record with private;"), "Abstract in C++: extend"), "class remarks");
      Check (Has (Doc ("function Area (Self : Shape_Record) return Long_Float;"), "Pure virtual in C++"), "pure virtual methods marked");

      --  Policy modules: features::data functions are in the nested package Data.
      U := Data.Unit;
      Check (U.X = 1.0 and then U.Y = 0.0, "nested package result");
      Free (U);
      Check (Data.Scaled (3) = 6 and then Data.Scaled (2.5) = 6.25, "nested package overloads");
      Check (Data.Repeat ("ab") = "abab" and then Repeat ('x', 3) = "xxx", "nested package names are separate");
      Check (Has (Doc ("function Unit return Vec2;"), "Mapped by the policy"), "nested package docs");
   end Test_Docs;

   File : Ada.Text_IO.File_Type;
begin
   Ada.Text_IO.Open (File, Ada.Text_IO.In_File, Ada.Command_Line.Argument (1));
   while not Ada.Text_IO.End_Of_File (File) loop
      Source.Append (Ada.Text_IO.Get_Line (File));
   end loop;
   Ada.Text_IO.Close (File);
   Test_Classes;
   Test_Values;
   Test_Objects;
   Test_Extensions;
   Test_Docs;
   Ada.Text_IO.Put_Line ("Ada API passed (" & Ada.Strings.Fixed.Trim (Checks'Image, Ada.Strings.Left) & " checks)");
exception
   when Failed => Ada.Command_Line.Set_Exit_Status (1);
end Test_Ada_Full;
