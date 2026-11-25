#pragma once

// Ada side of the Ada backend (--language ada; src/ada_full.cpp): the runtime in the visible and private
// parts of the generated package spec, and at the top of its body (the nested package Rosetta). It speaks
// the value encoding of lisp_full_runtime.hpp (the C++ library every full C ABI backend shares) through
// the native library's C entry points. @C@ is the prefix of the entry points, @LIB@ the library name.
// (Split in parts: compilers limit string literal sizes.)
namespace rosetta::detail {
inline constexpr const char* ada_full_runtime_spec_public[] = {
R"ada(   pragma Linker_Options ("-l@LIB@");

   ---------------------------------------------------------------------------
   --  Errors
   ---------------------------------------------------------------------------

   Native_Error : exception;
   --  Another C++ exception.
   Invalid_Argument_Error : exception;
   --  C++ std::invalid_argument.
   Out_Of_Range_Error : exception;
   --  C++ std::out_of_range.
   Domain_Error : exception;
   --  C++ std::domain_error.
   Length_Error : exception;
   --  C++ std::length_error.
   Arithmetic_Error : exception;
   --  C++ std::overflow_error, std::underflow_error or std::range_error.
   Allocation_Error : exception;
   --  C++ std::bad_alloc, or no memory for a call.
   Binding_Error : exception;
   --  A value C++ does not accept, a closed object, or another binding error.
   Callback_Error : exception;
   --  An Ada callback failed without an Ada exception to raise again.

   type Native_Error_Kind is
     (Native, Invalid_Argument, Out_Of_Range, Domain, Length, Arithmetic, Allocation, Binding, Callback, Not_Native);
   --  The kinds of the exceptions above (Not_Native: another exception).

   function Error_Kind (Occurrence : Ada.Exceptions.Exception_Occurrence) return Native_Error_Kind;
   --  The kind of an exception raised by a call.

   ---------------------------------------------------------------------------
   --  Values
   ---------------------------------------------------------------------------

   subtype Complex is Ada.Numerics.Long_Complex_Types.Complex;
   --  A C++ std::complex<double>.

   type Native_Value is private;
   --  A C++ value with no Ada type, kept as Rosetta's encoding: pass it back to C++ as it is.

   No_Value : constant Native_Value;
   --  The empty value (C++ receives nothing).

   ---------------------------------------------------------------------------
   --  Objects
   ---------------------------------------------------------------------------

   type Native_Object_Record is tagged limited private;
   --  The base of the types over C++ objects. Each C++ class is a type extension of it
   --  (Circle_Record) with an access type (Circle) that functions take and return.
   --
   --  Free the objects you create and the objects functions return by value; an object a
   --  C++ function returns by reference or pointer into another object belongs to that
   --  object, and is freed with it. Freeing an object that owns its C++ object (one you
   --  created, or a value) deletes the C++ object; Close deletes it at once and leaves the
   --  Ada object closed. Objects of your Ada type extensions come back from C++ as
   --  themselves.

   type Native_Object is access all Native_Object_Record'Class;
   --  Any object.

   procedure Close (Self : in out Native_Object_Record);
   --  Deletes the C++ object now if this object owns it; later calls raise Binding_Error.

   function Is_Closed (Self : Native_Object_Record) return Boolean;
   --  Whether Close was called, or C++ took over the object.

   function Image (Self : Native_Object_Record) return String;
   --  The class name, and whether the object is closed.

   procedure Free (Object : in out Native_Object);
   --  Frees an object (and the objects it owns), and sets Object to null.

   type Native_Function_Record is new Native_Object_Record with private;
   --  The base of C++ std::function values: each type has a Call function.
)ada"
};
inline constexpr const char* ada_full_runtime_spec_private[] = {
R"ada(   type Native_Object_Record is new Ada.Finalization.Limited_Controlled with record
      Rosetta_Holder : Interfaces.Unsigned_64 := 0;   --  the C++ object's holder (0: closed)
      Rosetta_Class : Interfaces.Unsigned_32 := 0;    --  its class table index
      Rosetta_Id : Interfaces.Unsigned_64 := 0;       --  its id for C++ (0: none yet)
      Rosetta_Shared : Boolean := False;              --  a cast sharing its owner's holder
      Rosetta_Key : Integer := 0;                     --  what it is to its owner
      Rosetta_Owner : Native_Object;                  --  the object it belongs to
      Rosetta_First : Native_Object;                  --  the objects it owns
      Rosetta_Next : Native_Object;                   --  the next object its owner owns
   end record;
   overriding procedure Finalize (Self : in out Native_Object_Record);

   type Native_Function_Record is new Native_Object_Record with null record;

   type Native_Value is record
      Data : Ada.Strings.Unbounded.Unbounded_String;
   end record;

   No_Value : constant Native_Value := (Data => Ada.Strings.Unbounded.Null_Unbounded_String);
)ada"
};
inline constexpr const char* ada_full_runtime_body[] = {
R"ada(   ---------------------------------------------------------------------------
   --  The runtime: values to and from C++, calls, the id table and the native library
   ---------------------------------------------------------------------------

   package Rosetta is
      use type Interfaces.Unsigned_8, Interfaces.Unsigned_32, Interfaces.Unsigned_64, Interfaces.Integer_64;
      use type Interfaces.C.int, Interfaces.C.size_t, System.Address, Ada.Exceptions.Exception_Occurrence_Access;
      subtype Size_T is Interfaces.C.size_t;

      package Id_Vectors is new Ada.Containers.Vectors (Positive, Interfaces.Unsigned_64);
      package Object_Vectors is new Ada.Containers.Vectors (Natural, Native_Object);

      --  Encodes values for C++.
      type Writer is record
         Data : Ada.Strings.Unbounded.Unbounded_String;
         Callbacks : Id_Vectors.Vector;   --  Ada callbacks registered for the call
      end record;

      procedure U8 (W : in out Writer; V : Interfaces.Unsigned_8);
      procedure U32 (W : in out Writer; V : Interfaces.Unsigned_32);
      procedure U64 (W : in out Writer; V : Interfaces.Unsigned_64);
      procedure Put_Nil (W : in out Writer);
      procedure Put_Bool (W : in out Writer; V : Boolean);
      procedure Put_Int (W : in out Writer; V : Interfaces.Integer_64);
      procedure Put_Unsigned (W : in out Writer; V : Interfaces.Unsigned_64);
      procedure Put_Real (W : in out Writer; V : Long_Float);
      procedure Put_String (W : in out Writer; V : String);
      procedure Put_Character (W : in out Writer; V : Character);
      procedure Put_Complex (W : in out Writer; V : Complex);
      procedure Put_Value (W : in out Writer; V : Native_Value);
      procedure Put_List (W : in out Writer; N : Natural);
      procedure Put_Map (W : in out Writer; N : Natural);
      procedure Put_Object (W : in out Writer; V : access Native_Object_Record'Class);
      procedure Put_Holder (W : in out Writer; H : Interfaces.Unsigned_64);

      --  Decodes values from C++: a reply (whose buffer it frees) or the arguments of a call from C++.
      type Buffer is new Ada.Finalization.Limited_Controlled with record
         Data : System.Address := System.Null_Address;
         Owned : Boolean := False;          --  free Data at the end (a reply)
      end record;
      overriding procedure Finalize (B : in out Buffer);

      type Reader is limited record
         Memory : Buffer;
         Size : Size_T := 0;
         Pos : Size_T := 0;
         Raw_Reals : Natural := 0;          --  untagged reals left in a real array
         Raw_Bytes : Natural := 0;          --  bytes left in a byte string read as integers
         Objects : Object_Vectors.Vector;   --  the call's object arguments
      end record;

      function Next_Tag (R : in out Reader) return Interfaces.Unsigned_8;
      function Take_Nil (R : in out Reader) return Boolean;
      function Get_Int (R : in out Reader; Min, Max : Interfaces.Integer_64; Name : String) return Interfaces.Integer_64;
      function Get_Unsigned_64 (R : in out Reader) return Interfaces.Unsigned_64;
      function Get_Bool (R : in out Reader) return Boolean;
      function Get_Real (R : in out Reader) return Long_Float;
      function Get_String (R : in out Reader) return String;
      function Get_Character (R : in out Reader) return Character;
      function Get_Complex (R : in out Reader) return Complex;
      function Get_Value (R : in out Reader) return Native_Value;
      function List_Length (R : in out Reader; Name : String) return Natural;
      function Map_Length (R : in out Reader; Pairs : out Boolean) return Natural;
      procedure Entry_Start (R : in out Reader; Pairs : Boolean);
      procedure Tuple_Start (R : in out Reader; N : Natural; Name : String);
      procedure Skip (R : in out Reader);
      function Count (R : in out Reader) return Natural;
      function Slot (R : in out Reader) return Natural;
      procedure Arity (Count, N : Natural);

      --  Ada subprograms passed to C++: generated extensions call the subprogram.
      type Callback_Record is abstract tagged limited null record;
      procedure Invoke_Callback (Self : in out Callback_Record; R : in out Reader; Count : Natural; W : in out Writer) is abstract;
      type Callback_Access is access all Callback_Record'Class;
      procedure Put_Callback (W : in out Writer; V : Callback_Access);

      --  One call into the native library: Start, the arguments (Arg, Self_Arg or Object_Arg, then
      --  the value), Run, then the result and the write-backs from the reader.
      type Call is record
         Index : Interfaces.Unsigned_32 := 0;
         W : Writer;
         Count : Interfaces.Unsigned_32 := 0;
         Objects : Object_Vectors.Vector;
      end record;
      function Start (Index : Interfaces.Unsigned_32) return Call;
      procedure Arg (Item : in out Call);
      procedure Slot (Item : in out Call; N : Natural);
      procedure Object_Arg (Item : in out Call; V : access Native_Object_Record'Class);
      procedure Self_Arg (Item : in out Call; V : Native_Object_Record'Class);
      procedure Run (Item : in out Call; R : in out Reader);
      function Start_Construct (Obj : Native_Object_Record'Class; Index : Interfaces.Unsigned_32; Director : Boolean) return Call;
      procedure Finish (Obj : in out Native_Object_Record'Class; Item : in out Call);

      --  Objects.
      function Object_Of (V : Native_Object_Record'Class) return Native_Object;
      procedure Free_Object is new Ada.Unchecked_Deallocation (Native_Object_Record'Class, Native_Object);
      procedure Release (Holder : Interfaces.Unsigned_64; Now : Boolean);
      procedure Forget (Id : Interfaces.Unsigned_64);
      procedure Adopt (Owner, Obj : Native_Object; Key : Integer);
      function Find (Owner : Native_Object; Key : Integer) return Native_Object;

      --  The C++ classes (filled when the package is elaborated).
      type Maker is access function return Native_Object;
      type Index_Array is array (Positive range <>) of Natural;
      type Index_Array_Access is access constant Index_Array;
      type Class_Info is record
         Name : Ada.Strings.Unbounded.Unbounded_String;
         Tag : Ada.Tags.Tag;
         Make : Maker;
         Bases : Index_Array_Access;
      end record;
      type Class_Array is array (Natural range <>) of Class_Info;
      type Class_Array_Access is access Class_Array;
      Classes : Class_Array_Access;
      Function_Class : constant Interfaces.Unsigned_32 := 16#FFFF_FFFF#;
      function Index_Of (T : Ada.Tags.Tag) return Integer;
      function Cast (Obj : Native_Object; Wanted : Natural) return Native_Object;
      function Get_Object (R : in out Reader; Wanted : Natural) return Native_Object;
      function Get_Function (R : in out Reader; Make : Maker) return Native_Object;

      --  The Ada methods C++ calls for the virtual methods of Ada type extensions.
      type Dispatch_Function is access function
        (Obj : Native_Object; Method : Natural; R : in out Reader; Count : Natural; W : in out Writer) return Boolean;
      Dispatcher : Dispatch_Function;

      procedure Initialize_Library;
   end Rosetta;

   package body Rosetta is
      use Ada.Strings.Unbounded;

      ------------------------------------------------------------ the native library
      type Invoke_Access is access function
        (Kind : Interfaces.C.int; Id : Interfaces.Unsigned_64; Method : Interfaces.C.unsigned; Data : System.Address; Size : Size_T;
         Output : out System.Address; Output_Size : out Size_T) return Interfaces.C.int
        with Convention => C;
      type Retain_Access is access procedure (Id : Interfaces.Unsigned_64; Delta_Refs : Interfaces.C.int) with Convention => C;

      function Native_Call
        (Index : Interfaces.C.unsigned; Data : System.Address; Size : Size_T; Output : out System.Address; Output_Size : out Size_T)
         return Interfaces.C.int
        with Import, Convention => C, External_Name => "@C@_call";
      procedure Native_Free (Pointer : System.Address) with Import, Convention => C, External_Name => "@C@_free";
      function Native_Alloc (Size : Size_T) return System.Address with Import, Convention => C, External_Name => "@C@_alloc";
      procedure Native_Release (Holder : System.Address; Now : Interfaces.C.int)
        with Import, Convention => C, External_Name => "@C@_release";
      function Native_Init (Version : Interfaces.C.int; Invoke : Invoke_Access; Retain : Retain_Access) return Interfaces.C.int
        with Import, Convention => C, External_Name => "@C@_init";

      function To_Address (V : Interfaces.Unsigned_64) return System.Address is
        (System.Storage_Elements.To_Address (System.Storage_Elements.Integer_Address (V)));
      function To_Bits is new Ada.Unchecked_Conversion (Long_Float, Interfaces.Unsigned_64);
      function Signed is new Ada.Unchecked_Conversion (Interfaces.Unsigned_64, Interfaces.Integer_64);
      function To_Real is new Ada.Unchecked_Conversion (Interfaces.Unsigned_64, Long_Float);

      ------------------------------------------------------------ the id table
      type Table_Entry is record
         Object : Native_Object;
         Callback : Callback_Access;
         Refs : Integer := 0;
         Cause : Ada.Exceptions.Exception_Occurrence_Access;
      end record;
      package Entry_Maps is new Ada.Containers.Ordered_Maps (Interfaces.Unsigned_64, Table_Entry);

      --  What C++ refers to by id: Ada objects, Ada callbacks, and the exceptions of callbacks.
      protected Table is
         procedure Add (Item : Table_Entry; Id : out Interfaces.Unsigned_64);
         procedure Remove (Id : Interfaces.Unsigned_64);
         function Find (Id : Interfaces.Unsigned_64) return Table_Entry;
         procedure Adjust (Id : Interfaces.Unsigned_64; Delta_Refs : Integer; Dropped : out Callback_Access);
         procedure Take (Id : Interfaces.Unsigned_64; Item : out Table_Entry);
      private
         Next : Interfaces.Unsigned_64 := 0;
         Entries : Entry_Maps.Map;
      end Table;

      protected body Table is
         procedure Add (Item : Table_Entry; Id : out Interfaces.Unsigned_64) is
         begin
            Next := Next + 1;
            Id := Next;
            Entries.Insert (Id, Item);
         end Add;

         procedure Remove (Id : Interfaces.Unsigned_64) is
         begin
            Entries.Exclude (Id);
         end Remove;

         function Find (Id : Interfaces.Unsigned_64) return Table_Entry is
            Position : constant Entry_Maps.Cursor := Entries.Find (Id);
         begin
            if Entry_Maps.Has_Element (Position) then
               return Entry_Maps.Element (Position);
            end if;
            return (others => <>);
         end Find;

         procedure Adjust (Id : Interfaces.Unsigned_64; Delta_Refs : Integer; Dropped : out Callback_Access) is
            Position : constant Entry_Maps.Cursor := Entries.Find (Id);
            Item : Table_Entry;
         begin
            Dropped := null;
            if not Entry_Maps.Has_Element (Position) then
               return;
            end if;
            Item := Entry_Maps.Element (Position);
            if Item.Callback = null then
               return;
            end if;
            Item.Refs := Item.Refs + Delta_Refs;
            if Item.Refs <= 0 then
               Dropped := Item.Callback;
               Entries.Delete (Id);
            else
               Entries.Replace (Id, Item);
            end if;
         end Adjust;

         procedure Take (Id : Interfaces.Unsigned_64; Item : out Table_Entry) is
            Position : constant Entry_Maps.Cursor := Entries.Find (Id);
         begin
            Item := (others => <>);
            if Entry_Maps.Has_Element (Position) then
               Item := Entry_Maps.Element (Position);
               Entries.Delete (Id);
            end if;
         end Take;
      end Table;

)ada",
R"ada(      procedure Free_Callback is new Ada.Unchecked_Deallocation (Callback_Record'Class, Callback_Access);

      procedure Adjust (Id : Interfaces.Unsigned_64; Delta_Refs : Integer) is
         Dropped : Callback_Access;
      begin
         Table.Adjust (Id, Delta_Refs, Dropped);
         if Dropped /= null then
            Free_Callback (Dropped);
         end if;
      end Adjust;

      function Id_Of (Obj : Native_Object) return Interfaces.Unsigned_64 is
      begin
         if Obj.Rosetta_Id = 0 then
            Table.Add ((Object => Obj, others => <>), Obj.Rosetta_Id);
         end if;
         return Obj.Rosetta_Id;
      end Id_Of;

      procedure Forget (Id : Interfaces.Unsigned_64) is
      begin
         Table.Remove (Id);
      end Forget;

      --  An exception of an Ada callback: C++ carries its message back, and the call that reached
      --  C++ raises the original exception again.
      Cause_Marker : constant String := " [ada callback #";

      function Save_Cause (E : Ada.Exceptions.Exception_Occurrence) return String is
         Id : Interfaces.Unsigned_64;
         Image : constant String := Ada.Exceptions.Exception_Message (E);
      begin
         Table.Add ((Cause => Ada.Exceptions.Save_Occurrence (E), others => <>), Id);
         return (if Image = "" then Ada.Exceptions.Exception_Name (E) else Image) & Cause_Marker & Interfaces.Unsigned_64'Image (Id) & "]";
      end Save_Cause;

      procedure Raise_Cause (Message : String) with No_Return is
         At_Marker : constant Natural := Ada.Strings.Fixed.Index (Message, Cause_Marker, Ada.Strings.Backward);
         Item : Table_Entry;
      begin
         if At_Marker > 0 and then Message (Message'Last) = ']' then
            declare
               Id : Interfaces.Unsigned_64;
            begin
               Id := Interfaces.Unsigned_64'Value (Message (At_Marker + Cause_Marker'Length .. Message'Last - 1));
               Table.Take (Id, Item);
            exception
               when Constraint_Error => null;
            end;
            if Item.Cause /= null then
               declare
                  Cause : Ada.Exceptions.Exception_Occurrence_Access := Item.Cause;
                  procedure Free_Cause is new Ada.Unchecked_Deallocation
                    (Ada.Exceptions.Exception_Occurrence, Ada.Exceptions.Exception_Occurrence_Access);
                  Saved : Ada.Exceptions.Exception_Occurrence;
               begin
                  Ada.Exceptions.Save_Occurrence (Saved, Cause.all);
                  Free_Cause (Cause);
                  Ada.Exceptions.Reraise_Occurrence (Saved);
               end;
            end if;
            raise Callback_Error with Message (Message'First .. At_Marker - 1);
         end if;
         raise Callback_Error with Message;
      end Raise_Cause;

      ------------------------------------------------------------ encoding (Ada -> C++)
      procedure U8 (W : in out Writer; V : Interfaces.Unsigned_8) is
      begin
         Append (W.Data, Character'Val (V));
      end U8;

      procedure U32 (W : in out Writer; V : Interfaces.Unsigned_32) is
         Bits : Interfaces.Unsigned_32 := V;
      begin
         for K in 1 .. 4 loop
            U8 (W, Interfaces.Unsigned_8 (Bits and 16#FF#));
            Bits := Interfaces.Shift_Right (Bits, 8);
         end loop;
      end U32;

      procedure U64 (W : in out Writer; V : Interfaces.Unsigned_64) is
         Bits : Interfaces.Unsigned_64 := V;
      begin
         for K in 1 .. 8 loop
            U8 (W, Interfaces.Unsigned_8 (Bits and 16#FF#));
            Bits := Interfaces.Shift_Right (Bits, 8);
         end loop;
      end U64;

      procedure Text (W : in out Writer; Tag : Interfaces.Unsigned_8; S : String) is
      begin
         U8 (W, Tag);
         U64 (W, Interfaces.Unsigned_64 (S'Length));
         Append (W.Data, S);
      end Text;

      procedure Put_Nil (W : in out Writer) is
      begin
         U8 (W, 0);
      end Put_Nil;

      procedure Put_Bool (W : in out Writer; V : Boolean) is
      begin
         U8 (W, (if V then 1 else 19));
      end Put_Bool;

      procedure Put_Int (W : in out Writer; V : Interfaces.Integer_64) is
         function Bits is new Ada.Unchecked_Conversion (Interfaces.Integer_64, Interfaces.Unsigned_64);
      begin
         U8 (W, 2);
         U64 (W, Bits (V));
      end Put_Int;

      procedure Put_Unsigned (W : in out Writer; V : Interfaces.Unsigned_64) is
         Image : constant String := Interfaces.Unsigned_64'Image (V);
      begin
         if V > Interfaces.Unsigned_64 (Interfaces.Integer_64'Last) then
            Text (W, 3, Image (Image'First + 1 .. Image'Last));
         else
            Put_Int (W, Interfaces.Integer_64 (V));
         end if;
      end Put_Unsigned;

      procedure Put_Real (W : in out Writer; V : Long_Float) is
      begin
         U8 (W, 4);
         U64 (W, To_Bits (V));
      end Put_Real;

      procedure Put_String (W : in out Writer; V : String) is
      begin
         Text (W, 5, V);
      end Put_String;

      procedure Put_Character (W : in out Writer; V : Character) is
      begin
         Text (W, 5, [1 => V]);
      end Put_Character;

      procedure Put_Complex (W : in out Writer; V : Complex) is
      begin
         U8 (W, 18);
         U64 (W, To_Bits (V.Re));
         U64 (W, To_Bits (V.Im));
      end Put_Complex;

      procedure Put_Value (W : in out Writer; V : Native_Value) is
      begin
         if Length (V.Data) = 0 then
            Put_Nil (W);
         else
            Append (W.Data, V.Data);
         end if;
      end Put_Value;

      procedure Put_List (W : in out Writer; N : Natural) is
      begin
         U8 (W, 6);
         U64 (W, Interfaces.Unsigned_64 (N));
      end Put_List;

      procedure Put_Map (W : in out Writer; N : Natural) is
      begin
         U8 (W, 9);
         U64 (W, Interfaces.Unsigned_64 (N));
      end Put_Map;

      procedure Put_Object (W : in out Writer; V : access Native_Object_Record'Class) is
      begin
         if V = null then
            Put_Nil (W);
            return;
         end if;
         U8 (W, 10);
         U64 (W, V.Rosetta_Holder);
         if V.Rosetta_Holder = 0 then
            U64 (W, 0);
         else
            U64 (W, Id_Of (V.all'Unchecked_Access));
         end if;
      end Put_Object;

      procedure Put_Holder (W : in out Writer; H : Interfaces.Unsigned_64) is
      begin
         U8 (W, 10);
         U64 (W, H);
         U64 (W, 0);
      end Put_Holder;

      procedure Put_Callback (W : in out Writer; V : Callback_Access) is
         Id : Interfaces.Unsigned_64;
      begin
         if V = null then
            Put_Nil (W);
            return;
         end if;
         Table.Add ((Callback => V, Refs => 1, others => <>), Id);
         W.Callbacks.Append (Id);
         U8 (W, 12);
         U64 (W, Id);
      end Put_Callback;

      procedure Drop_Callbacks (W : in out Writer) is
      begin
         for Id of W.Callbacks loop
            Adjust (Id, -1);
         end loop;
         W.Callbacks.Clear;
      end Drop_Callbacks;

      ------------------------------------------------------------ decoding (C++ -> Ada)
      overriding procedure Finalize (B : in out Buffer) is
      begin
         if B.Owned and then B.Data /= System.Null_Address then
            Native_Free (B.Data);
         end if;
         B.Data := System.Null_Address;
         B.Owned := False;
      end Finalize;

      procedure Need (R : Reader; N : Size_T) is
      begin
         if N > R.Size - R.Pos then
            raise Binding_Error with "malformed native value";
         end if;
      end Need;

      function U8 (R : in out Reader) return Interfaces.Unsigned_8 is
      begin
         Need (R, 1);
         declare
            use System.Storage_Elements;
            V : Interfaces.Unsigned_8 with Import, Address => R.Memory.Data + Storage_Offset (R.Pos);
         begin
            R.Pos := R.Pos + 1;
            return V;
         end;
      end U8;

      function U32 (R : in out Reader) return Interfaces.Unsigned_32 is
         V : Interfaces.Unsigned_32 := 0;
      begin
         for K in 0 .. 3 loop
            V := V or Interfaces.Shift_Left (Interfaces.Unsigned_32 (U8 (R)), 8 * K);
         end loop;
         return V;
      end U32;

      function U64 (R : in out Reader) return Interfaces.Unsigned_64 is
         V : Interfaces.Unsigned_64 := 0;
      begin
         for K in 0 .. 7 loop
            V := V or Interfaces.Shift_Left (Interfaces.Unsigned_64 (U8 (R)), 8 * K);
         end loop;
         return V;
      end U64;

      function Bytes (R : in out Reader) return String is
         N : constant Interfaces.Unsigned_64 := U64 (R);
      begin
         if N > Interfaces.Unsigned_64 (R.Size - R.Pos) then
            raise Binding_Error with "malformed native value";
         end if;
         declare
            use System.Storage_Elements;
            V : String (1 .. Natural (N)) with Import, Address => R.Memory.Data + Storage_Offset (R.Pos);
         begin
            R.Pos := R.Pos + Size_T (N);
            return V;
         end;
      end Bytes;

      --  Tags: 0 nil, 1 true, 19 false, 2 integer, 3 big integer, 4 real, 5 string, 6 list, 7 real
      --  array, 8 pair, 9 map, 10 new object, 11 Ada object, 12 function, 14 symbol, 15 bytes,
      --  16 tuple, 17 argument reference, 18 complex; 200 is a byte of a byte string read as an
      --  integer.
      function Next_Tag (R : in out Reader) return Interfaces.Unsigned_8 is
      begin
         if R.Raw_Reals > 0 then
            R.Raw_Reals := R.Raw_Reals - 1;
            return 4;
         elsif R.Raw_Bytes > 0 then
            R.Raw_Bytes := R.Raw_Bytes - 1;
            return 200;
         end if;
         return U8 (R);
      end Next_Tag;

      function Take_Nil (R : in out Reader) return Boolean is
         use System.Storage_Elements;
      begin
         if R.Raw_Reals > 0 or else R.Raw_Bytes > 0 then
            return False;
         end if;
         Need (R, 1);
         declare
            V : Interfaces.Unsigned_8 with Import, Address => R.Memory.Data + Storage_Offset (R.Pos);
         begin
            if V = 0 then
               R.Pos := R.Pos + 1;
               return True;
            end if;
            return False;
         end;
      end Take_Nil;

      function Kind_Name (Tag : Interfaces.Unsigned_8) return String is
        (case Tag is
            when 0 => "null",
            when 1 | 19 => "a Boolean",
            when 2 | 3 | 200 => "an integer",
            when 4 => "a real",
            when 5 | 14 => "a string",
            when 6 | 7 | 8 | 16 => "a list",
            when 9 => "a map",
            when 10 | 11 | 17 => "an object",
            when 12 => "a function",
            when 15 => "bytes",
            when 18 => "a complex",
            when others => "a value");

      procedure Mismatch (Tag : Interfaces.Unsigned_8; Wanted : String) with No_Return is
      begin
         raise Binding_Error with "cannot convert " & Kind_Name (Tag) & " to " & Wanted;
      end Mismatch;

      function Get_Int (R : in out Reader; Min, Max : Interfaces.Integer_64; Name : String) return Interfaces.Integer_64 is
         Tag : constant Interfaces.Unsigned_8 := Next_Tag (R);
         V : Interfaces.Integer_64;
      begin
         case Tag is
            when 2 => V := Signed (U64 (R));
            when 200 => V := Interfaces.Integer_64 (U8 (R));
            when 3 =>
               declare
                  Image : constant String := Bytes (R);
               begin
                  V := Interfaces.Integer_64'Value (Image);
               exception
                  when Constraint_Error => raise Binding_Error with "integer " & Image & " is out of range for " & Name;
               end;
            when 4 =>
               declare
                  Real : constant Long_Float := To_Real (U64 (R));
               begin
                  if Real /= Long_Float'Truncation (Real) or else abs Real > 9.2E18 then
                     Mismatch (Tag, Name);
                  end if;
                  V := Interfaces.Integer_64 (Real);
               end;
            when 1 => V := 1;
            when 19 => V := 0;
            when others => Mismatch (Tag, Name);
         end case;
         if V < Min or else V > Max then
            raise Binding_Error with "integer" & Interfaces.Integer_64'Image (V) & " is out of range for " & Name;
         end if;
         return V;
      end Get_Int;

)ada",
R"ada(      function Get_Unsigned_64 (R : in out Reader) return Interfaces.Unsigned_64 is
         Tag : constant Interfaces.Unsigned_8 := Next_Tag (R);
      begin
         case Tag is
            when 3 =>
               declare
                  Image : constant String := Bytes (R);
               begin
                  return Interfaces.Unsigned_64'Value (Image);
               exception
                  when Constraint_Error => raise Binding_Error with "integer " & Image & " is out of range for Unsigned_64";
               end;
            when 2 =>
               declare
                  V : constant Interfaces.Integer_64 := Signed (U64 (R));
               begin
                  if V < 0 then
                     raise Binding_Error with "integer" & V'Image & " is out of range for Unsigned_64";
                  end if;
                  return Interfaces.Unsigned_64 (V);
               end;
            when 200 => return Interfaces.Unsigned_64 (U8 (R));
            when 4 =>
               declare
                  Real : constant Long_Float := To_Real (U64 (R));
               begin
                  if Real /= Long_Float'Truncation (Real) or else Real < 0.0 or else Real > 1.8E19 then
                     Mismatch (Tag, "Unsigned_64");
                  end if;
                  return Interfaces.Unsigned_64 (Real);
               end;
            when 1 => return 1;
            when 19 => return 0;
            when others => Mismatch (Tag, "Unsigned_64");
         end case;
      end Get_Unsigned_64;

      function Get_Bool (R : in out Reader) return Boolean is
         Tag : constant Interfaces.Unsigned_8 := Next_Tag (R);
      begin
         case Tag is
            when 1 => return True;
            when 0 | 19 => return False;
            when 2 => return U64 (R) /= 0;
            when others => Mismatch (Tag, "Boolean");
         end case;
      end Get_Bool;

      function Get_Real (R : in out Reader) return Long_Float is
         Tag : constant Interfaces.Unsigned_8 := Next_Tag (R);
      begin
         case Tag is
            when 4 => return To_Real (U64 (R));
            when 2 => return Long_Float (Signed (U64 (R)));
            when 200 => return Long_Float (U8 (R));
            when 3 => return Long_Float'Value (Bytes (R));
            when others => Mismatch (Tag, "Long_Float");
         end case;
      end Get_Real;

      function Get_String (R : in out Reader) return String is
         Tag : constant Interfaces.Unsigned_8 := Next_Tag (R);
      begin
         case Tag is
            when 5 | 14 | 15 => return Bytes (R);
            when 0 => return "";
            when others => Mismatch (Tag, "String");
         end case;
      end Get_String;

      function Get_Character (R : in out Reader) return Character is
         Tag : constant Interfaces.Unsigned_8 := Next_Tag (R);
      begin
         case Tag is
            when 5 | 15 =>
               declare
                  S : constant String := Bytes (R);
               begin
                  if S'Length = 1 then
                     return S (S'First);
                  end if;
               end;
            when 2 =>
               declare
                  V : constant Interfaces.Unsigned_64 := U64 (R);
               begin
                  if V <= 255 then
                     return Character'Val (V);
                  end if;
               end;
            when others => null;
         end case;
         Mismatch (Tag, "Character");
      end Get_Character;

      function Get_Complex (R : in out Reader) return Complex is
      begin
         if not (R.Raw_Reals > 0 or else R.Raw_Bytes > 0) then
            declare
               use System.Storage_Elements;
               Tag : Interfaces.Unsigned_8 with Import, Address => R.Memory.Data + Storage_Offset (R.Pos);
            begin
               Need (R, 1);
               if Tag = 18 then
                  R.Pos := R.Pos + 1;
                  declare
                     Re : constant Long_Float := To_Real (U64 (R));
                     Im : constant Long_Float := To_Real (U64 (R));
                  begin
                     return (Re => Re, Im => Im);
                  end;
               end if;
            end;
         end if;
         return (Re => Get_Real (R), Im => 0.0);
      end Get_Complex;

      --  Copies a value from C++ into W, as Ada sends it to C++.
      procedure Copy (R : in out Reader; W : in out Writer) is
         Tag : constant Interfaces.Unsigned_8 := Next_Tag (R);
      begin
         case Tag is
            when 0 | 1 | 19 => U8 (W, Tag);
            when 2 =>
               U8 (W, 2);
               U64 (W, U64 (R));
            when 200 => Put_Int (W, Interfaces.Integer_64 (U8 (R)));
            when 3 | 5 | 14 | 15 => Text (W, Tag, Bytes (R));
            when 4 =>
               U8 (W, 4);
               U64 (W, U64 (R));
            when 18 =>
               U8 (W, 18);
               U64 (W, U64 (R));
               U64 (W, U64 (R));
            when 6 | 16 =>
               declare
                  N : constant Interfaces.Unsigned_64 := U64 (R);
               begin
                  U8 (W, 6);
                  U64 (W, N);
                  for K in 1 .. N loop
                     Copy (R, W);
                  end loop;
               end;
            when 7 =>
               declare
                  N : constant Interfaces.Unsigned_64 := U64 (R);
               begin
                  U8 (W, 6);
                  U64 (W, N);
                  for K in 1 .. N loop
                     U8 (W, 4);
                     U64 (W, U64 (R));
                  end loop;
               end;
            when 8 =>
               Put_List (W, 2);
               Copy (R, W);
               Copy (R, W);
            when 9 =>
               declare
                  N : constant Interfaces.Unsigned_64 := U64 (R);
               begin
                  U8 (W, 9);
                  U64 (W, N);
                  for K in 1 .. 2 * N loop
                     Copy (R, W);
                  end loop;
               end;
            when 10 =>
               declare
                  Holder : constant Interfaces.Unsigned_64 := U64 (R);
                  Ignored_Class : constant Interfaces.Unsigned_32 := U32 (R);
                  Ignored_Parent : constant Interfaces.Unsigned_64 := U64 (R);
               begin
                  Put_Holder (W, Holder);
               end;
            when 11 => Put_Object (W, Table.Find (U64 (R)).Object);
            when 12 =>
               declare
                  Holder : constant Interfaces.Unsigned_64 := U64 (R);
               begin
                  U8 (W, 12);
                  U64 (W, Holder);
               end;
            when 17 =>
               declare
                  Index : constant Natural := Natural (U32 (R));
               begin
                  if Index < Natural (R.Objects.Length) then
                     Put_Object (W, R.Objects (Index));
                  else
                     Put_Nil (W);
                  end if;
               end;
            when others => raise Binding_Error with "malformed native value (tag" & Tag'Image & ")";
         end case;
      end Copy;

      function Get_Value (R : in out Reader) return Native_Value is
         W : Writer;
      begin
         Copy (R, W);
         return (Data => W.Data);
      end Get_Value;

      procedure Skip (R : in out Reader) is
         W : Writer;
      begin
         Copy (R, W);
      end Skip;

      function List_Length (R : in out Reader; Name : String) return Natural is
         Tag : constant Interfaces.Unsigned_8 := Next_Tag (R);
      begin
         case Tag is
            when 0 => return 0;
            when 6 | 16 => return Natural (U64 (R));
            when 8 => return 2;
            when 7 =>
               R.Raw_Reals := Natural (U64 (R));
               return R.Raw_Reals;
            when 5 | 15 =>
               R.Raw_Bytes := Natural (U64 (R));
               return R.Raw_Bytes;
            when others => Mismatch (Tag, Name);
         end case;
      end List_Length;

      function Map_Length (R : in out Reader; Pairs : out Boolean) return Natural is
         Tag : constant Interfaces.Unsigned_8 := Next_Tag (R);
      begin
         Pairs := False;
         case Tag is
            when 0 => return 0;
            when 9 => return Natural (U64 (R));
            when 6 | 16 =>
               Pairs := True;
               return Natural (U64 (R));
            when others => Mismatch (Tag, "a map");
         end case;
      end Map_Length;

      procedure Entry_Start (R : in out Reader; Pairs : Boolean) is
      begin
         if Pairs and then List_Length (R, "a map entry") /= 2 then
            raise Binding_Error with "cannot convert a list to a map entry";
         end if;
      end Entry_Start;

      procedure Tuple_Start (R : in out Reader; N : Natural; Name : String) is
      begin
         if List_Length (R, Name) /= N then
            raise Binding_Error with "cannot convert a list to " & Name;
         end if;
      end Tuple_Start;

      function Count (R : in out Reader) return Natural is (Natural (U32 (R)));
      function Slot (R : in out Reader) return Natural is (Natural (U32 (R)));

      procedure Arity (Count, N : Natural) is
      begin
         if Count /= N then
            raise Binding_Error with "Ada subprogram takes" & N'Image & " arguments, not" & Count'Image;
         end if;
      end Arity;

      ------------------------------------------------------------ objects
      package Conversions is new System.Address_To_Access_Conversions (Native_Object_Record'Class);

      function Object_Of (V : Native_Object_Record'Class) return Native_Object is
        (Native_Object (Conversions.To_Pointer (V'Address)));

      procedure Release (Holder : Interfaces.Unsigned_64; Now : Boolean) is
      begin
         Native_Release (To_Address (Holder), (if Now then 1 else 0));
      end Release;

      procedure Adopt (Owner, Obj : Native_Object; Key : Integer) is
      begin
         Obj.Rosetta_Owner := Owner;
         Obj.Rosetta_Key := Key;
         Obj.Rosetta_Next := Owner.Rosetta_First;
         Owner.Rosetta_First := Obj;
      end Adopt;

      function Find (Owner : Native_Object; Key : Integer) return Native_Object is
         Item : Native_Object := Owner.Rosetta_First;
      begin
         while Item /= null loop
            if Item.Rosetta_Key = Key then
               return Item;
            end if;
            Item := Item.Rosetta_Next;
         end loop;
         return null;
      end Find;

      function Derives (From, To : Natural) return Boolean is
      begin
         if From = To then
            return True;
         end if;
         if From > Classes'Last then
            return False;
         end if;
         for Base of Classes (From).Bases.all loop
            if Derives (Base, To) then
               return True;
            end if;
         end loop;
         return False;
      end Derives;

      function Index_Of (T : Ada.Tags.Tag) return Integer is
         use type Ada.Tags.Tag;
         Current : Ada.Tags.Tag := T;
      begin
         while Current /= Ada.Tags.No_Tag loop
            for K in Classes'Range loop
               if Classes (K).Tag = Current then
                  return K;
               end if;
            end loop;
            Current := Ada.Tags.Parent_Tag (Current);
         end loop;
         return -1;
      end Index_Of;

      function Is_A (Obj : Native_Object; Wanted : Natural) return Boolean is
        (Ada.Tags.Is_Descendant_At_Same_Level (Obj.all'Tag, Classes (Wanted).Tag));

      --  Obj as class Wanted: Obj itself, else (a further C++ base) an object sharing its C++ object
      --  that Obj owns, else null.
      function Cast (Obj : Native_Object; Wanted : Natural) return Native_Object is
         Result : Native_Object;
      begin
         if Obj = null or else Is_A (Obj, Wanted) then
            return Obj;
         end if;
         if Obj.Rosetta_Holder = 0 or else Obj.Rosetta_Class = Function_Class
           or else not Derives (Natural (Obj.Rosetta_Class), Wanted)
         then
            return null;
         end if;
         Result := Find (Obj, -1 - Wanted);
         if Result /= null then
            return Result;
         end if;
         Result := Classes (Wanted).Make.all;
         Result.Rosetta_Holder := Obj.Rosetta_Holder;
         Result.Rosetta_Class := Obj.Rosetta_Class;
         Result.Rosetta_Shared := True;
         Adopt (Obj, Result, -1 - Wanted);
         return Result;
      end Cast;

)ada",
R"ada(      function Existing (R : in out Reader; Tag : Interfaces.Unsigned_8) return Native_Object is
      begin
         if Tag = 11 then
            declare
               Obj : constant Native_Object := Table.Find (U64 (R)).Object;
            begin
               if Obj = null then
                  raise Binding_Error with "the Ada object of a native reference is gone";
               end if;
               return Obj;
            end;
         end if;
         declare
            Index : constant Natural := Natural (U32 (R));
         begin
            if Index < Natural (R.Objects.Length) then
               return R.Objects (Index);
            end if;
            return null;
         end;
      end Existing;

      function Get_Object (R : in out Reader; Wanted : Natural) return Native_Object is
         Tag : constant Interfaces.Unsigned_8 := Next_Tag (R);
         Wanted_Name : constant String := To_String (Classes (Wanted).Name);
      begin
         case Tag is
            when 0 => return null;
            when 10 =>
               declare
                  Holder : constant Interfaces.Unsigned_64 := U64 (R);
                  Class : constant Interfaces.Unsigned_32 := U32 (R);
                  Parent : constant Interfaces.Integer_64 := Signed (U64 (R));
                  Obj : Native_Object;
               begin
                  if Natural (Class) > Classes'Last then
                     Release (Holder, False);
                     raise Binding_Error with "C++ result type is not exported to Ada";
                  end if;
                  Obj := Classes (Natural (Class)).Make.all;
                  if not Is_A (Obj, Wanted) then
                     --  A further base of the C++ object: no Ada type extension.
                     Free_Object (Obj);
                     Obj := Classes (Wanted).Make.all;
                  end if;
                  Obj.Rosetta_Holder := Holder;
                  Obj.Rosetta_Class := Class;
                  if Parent >= 0 and then Parent < Interfaces.Integer_64 (R.Objects.Length) and then R.Objects (Natural (Parent)) /= null then
                     Adopt (R.Objects (Natural (Parent)), Obj, 0);
                  end if;
                  return Obj;
               end;
            when 11 | 17 =>
               declare
                  Result : constant Native_Object := Cast (Existing (R, Tag), Wanted);
               begin
                  if Result = null then
                     Mismatch (Tag, Wanted_Name);
                  end if;
                  return Result;
               end;
            when others => Mismatch (Tag, Wanted_Name);
         end case;
      end Get_Object;

      function Get_Function (R : in out Reader; Make : Maker) return Native_Object is
         Tag : constant Interfaces.Unsigned_8 := Next_Tag (R);
      begin
         case Tag is
            when 0 => return null;
            when 12 =>
               declare
                  Obj : constant Native_Object := Make.all;
               begin
                  Obj.Rosetta_Holder := U64 (R);
                  Obj.Rosetta_Class := Function_Class;
                  return Obj;
               end;
            when others => Mismatch (Tag, "a function");
         end case;
      end Get_Function;

      ------------------------------------------------------------ calls
      function Start (Index : Interfaces.Unsigned_32) return Call is
      begin
         return Result : Call do
            Result.Index := Index;
            U32 (Result.W, 0);   --  the argument count, set by Run
         end return;
      end Start;

      procedure Arg (Item : in out Call) is
      begin
         Item.Count := Item.Count + 1;
         Item.Objects.Append (null);
      end Arg;

      procedure Slot (Item : in out Call; N : Natural) is
      begin
         U8 (Item.W, 13);
         U32 (Item.W, Interfaces.Unsigned_32 (N));
      end Slot;

      procedure Object_Arg (Item : in out Call; V : access Native_Object_Record'Class) is
      begin
         Item.Count := Item.Count + 1;
         Item.Objects.Append ((if V = null then null else V.all'Unchecked_Access));
         Put_Object (Item.W, V);
      end Object_Arg;

      procedure Self_Arg (Item : in out Call; V : Native_Object_Record'Class) is
      begin
         Object_Arg (Item, Object_Of (V));
      end Self_Arg;

      procedure Run (Item : in out Call; R : in out Reader) is
         Output : System.Address := System.Null_Address;
         Output_Size : Size_T := 0;
         Status : Interfaces.C.int;
         Header : String (1 .. 4);
         Bits : Interfaces.Unsigned_32 := Item.Count;
      begin
         for K in Header'Range loop
            Header (K) := Character'Val (Bits and 16#FF#);
            Bits := Interfaces.Shift_Right (Bits, 8);
         end loop;
         Replace_Slice (Item.W.Data, 1, 4, Header);
         declare
            Input : aliased constant String := To_String (Item.W.Data);
         begin
            Status := Native_Call (Interfaces.C.unsigned (Item.Index), Input'Address, Input'Length, Output, Output_Size);
         end;
         Drop_Callbacks (Item.W);
         R.Memory.Data := Output;
         R.Memory.Owned := True;
         R.Size := Output_Size;
         R.Pos := 0;
         R.Objects := Item.Objects;
         case Status is
            when 0 => null;   --  the caller reads the result, then the write-backs
            when 1 | 2 =>
               declare
                  Kind : constant String := Get_String (R);
                  Message : constant String := Get_String (R);
               begin
                  if Status = 2 then
                     Raise_Cause (Message);
                  elsif Kind = "binding" then
                     raise Binding_Error with Message;
                  elsif Kind = "invalid-argument" then
                     raise Invalid_Argument_Error with Message;
                  elsif Kind = "out-of-range" then
                     raise Out_Of_Range_Error with Message;
                  elsif Kind = "domain-error" then
                     raise Domain_Error with Message;
                  elsif Kind = "length-error" then
                     raise Length_Error with Message;
                  elsif Kind = "arithmetic-error" then
                     raise Arithmetic_Error with Message;
                  elsif Kind = "allocation" then
                     raise Allocation_Error with Message;
                  end if;
                  raise Native_Error with Message;
               end;
            when others => raise Allocation_Error with "out of memory in a native call";
         end case;
      end Run;

      function Start_Construct (Obj : Native_Object_Record'Class; Index : Interfaces.Unsigned_32; Director : Boolean) return Call is
      begin
         return Result : Call := Start (Index) do
            Arg (Result);
            Put_Int (Result.W, (if Director then Interfaces.Integer_64 (Id_Of (Object_Of (Obj))) else 0));
         end return;
      end Start_Construct;

      procedure Finish (Obj : in out Native_Object_Record'Class; Item : in out Call) is
         R : Reader;
      begin
         Run (Item, R);
         if Next_Tag (R) /= 10 then
            raise Binding_Error with "C++ constructor returned no object";
         end if;
         Obj.Rosetta_Holder := U64 (R);
         Obj.Rosetta_Class := U32 (R);
      end Finish;

      ------------------------------------------------------------ calls from C++
      function Invoke
        (Kind : Interfaces.C.int; Id : Interfaces.Unsigned_64; Method : Interfaces.C.unsigned; Data : System.Address; Size : Size_T;
         Output : out System.Address; Output_Size : out Size_T) return Interfaces.C.int
        with Convention => C;

      function Invoke
        (Kind : Interfaces.C.int; Id : Interfaces.Unsigned_64; Method : Interfaces.C.unsigned; Data : System.Address; Size : Size_T;
         Output : out System.Address; Output_Size : out Size_T) return Interfaces.C.int
      is
         W : Writer;
         Status : Interfaces.C.int := 0;
      begin
         Output := System.Null_Address;
         Output_Size := 0;
         begin
            declare
               R : Reader;
            begin
               R.Memory.Data := Data;
               R.Size := Size;
               declare
                  N : constant Natural := Count (R);
               begin
                  if Kind = 0 then
                     declare
                        Target : constant Callback_Access := Table.Find (Id).Callback;
                     begin
                        if Target = null then
                           raise Binding_Error with "the Ada subprogram of a native reference is gone";
                        end if;
                        Target.Invoke_Callback (R, N, W);
                     end;
                  else
                     if N = 0 then
                        raise Binding_Error with "virtual method called without an object";
                     end if;
                     declare
                        Tag : constant Interfaces.Unsigned_8 := Next_Tag (R);
                        Obj : constant Native_Object := (if Tag in 11 | 17 then Existing (R, Tag) else null);
                     begin
                        if Obj = null or else Dispatcher = null or else not Dispatcher (Obj, Natural (Method), R, N - 1, W) then
                           raise Binding_Error with "no Ada method for C++ virtual method" & Method'Image;
                        end if;
                     end;
                  end if;
               end;
            end;
            --  Ada callbacks inside the result stay registered until C++ keeps or drops them.
            W.Callbacks.Clear;
         exception
            when E : others =>
               W := (others => <>);
               Put_String (W, Save_Cause (E));
               Status := 2;
         end;
         declare
            S : constant String := To_String (W.Data);
            Buffer : constant System.Address := Native_Alloc (S'Length + 1);
         begin
            if Buffer = System.Null_Address then
               return 3;
            end if;
            declare
               Target : String (1 .. S'Length) with Import, Address => Buffer;
            begin
               Target := S;
            end;
            Output := Buffer;
            Output_Size := S'Length;
         end;
         return Status;
      exception
         when others => return 3;
      end Invoke;

      procedure Retain (Id : Interfaces.Unsigned_64; Delta_Refs : Interfaces.C.int) with Convention => C;

      procedure Retain (Id : Interfaces.Unsigned_64; Delta_Refs : Interfaces.C.int) is
      begin
         Adjust (Id, Integer (Delta_Refs));
      exception
         when others => null;
      end Retain;

      procedure Initialize_Library is
      begin
         if Native_Init (1, Invoke'Access, Retain'Access) /= 0 then
            raise Binding_Error with "native library @LIB@ was generated by a different Rosetta version";
         end if;
      end Initialize_Library;
   end Rosetta;

   ---------------------------------------------------------------------------
   --  Errors and objects
   ---------------------------------------------------------------------------

   function Error_Kind (Occurrence : Ada.Exceptions.Exception_Occurrence) return Native_Error_Kind is
      use type Ada.Exceptions.Exception_Id;
      Id : constant Ada.Exceptions.Exception_Id := Ada.Exceptions.Exception_Identity (Occurrence);
   begin
      return
        (if Id = Native_Error'Identity then Native
         elsif Id = Invalid_Argument_Error'Identity then Invalid_Argument
         elsif Id = Out_Of_Range_Error'Identity then Out_Of_Range
         elsif Id = Domain_Error'Identity then Domain
         elsif Id = Length_Error'Identity then Length
         elsif Id = Arithmetic_Error'Identity then Arithmetic
         elsif Id = Allocation_Error'Identity then Allocation
         elsif Id = Binding_Error'Identity then Binding
         elsif Id = Callback_Error'Identity then Callback
         else Not_Native);
   end Error_Kind;

)ada",
R"ada(   procedure Close (Self : in out Native_Object_Record) is
      use type Interfaces.Unsigned_64;
      Item : Native_Object := Self.Rosetta_First;
   begin
      if Self.Rosetta_Holder /= 0 and then not Self.Rosetta_Shared then
         Rosetta.Release (Self.Rosetta_Holder, True);
      end if;
      Self.Rosetta_Holder := 0;
      while Item /= null loop
         if Item.Rosetta_Shared then
            Item.Rosetta_Holder := 0;
         end if;
         Item := Item.Rosetta_Next;
      end loop;
   end Close;

   function Is_Closed (Self : Native_Object_Record) return Boolean is
      use type Interfaces.Unsigned_64;
   begin
      return Self.Rosetta_Holder = 0;
   end Is_Closed;

   function Image (Self : Native_Object_Record) return String is
      Index : constant Integer := Rosetta.Index_Of (Native_Object_Record'Class (Self)'Tag);
      Name : constant String :=
        (if Index < 0 then Ada.Tags.Expanded_Name (Native_Object_Record'Class (Self)'Tag)
         else Ada.Strings.Unbounded.To_String (Rosetta.Classes (Index).Name));
   begin
      return (if Self.Is_Closed then Name & " (closed)" else Name);
   end Image;

   procedure Free (Object : in out Native_Object) is
   begin
      Rosetta.Free_Object (Object);
   end Free;

   overriding procedure Finalize (Self : in out Native_Object_Record) is
      use type Interfaces.Unsigned_64;
      Item : Native_Object := Self.Rosetta_First;
      Next : Native_Object;
   begin
      if Self.Rosetta_Id /= 0 then
         Rosetta.Forget (Self.Rosetta_Id);
         Self.Rosetta_Id := 0;
      end if;
      --  Owned objects go first: they may refer into this object.
      Self.Rosetta_First := null;
      while Item /= null loop
         Next := Item.Rosetta_Next;
         Item.Rosetta_Owner := null;
         Rosetta.Free_Object (Item);
         Item := Next;
      end loop;
      if Self.Rosetta_Holder /= 0 and then not Self.Rosetta_Shared then
         Rosetta.Release (Self.Rosetta_Holder, False);
      end if;
      Self.Rosetta_Holder := 0;
      if Self.Rosetta_Owner /= null then
         declare
            Owner : constant Native_Object := Self.Rosetta_Owner;
            Me : constant Native_Object := Rosetta.Object_Of (Native_Object_Record'Class (Self));
         begin
            if Owner.Rosetta_First = Me then
               Owner.Rosetta_First := Self.Rosetta_Next;
            else
               Item := Owner.Rosetta_First;
               while Item /= null and then Item.Rosetta_Next /= Me loop
                  Item := Item.Rosetta_Next;
               end loop;
               if Item /= null then
                  Item.Rosetta_Next := Self.Rosetta_Next;
               end if;
            end if;
            Self.Rosetta_Owner := null;
         end;
      end if;
   end Finalize;
)ada"
};
} // namespace rosetta::detail
