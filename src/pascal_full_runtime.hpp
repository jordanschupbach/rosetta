#pragma once

// Pascal side of the Pascal backend (--language pascal; src/pascal_full.cpp): the runtime at the top
// of the interface and of the implementation of the generated unit. It speaks the value encoding of
// lisp_full_runtime.hpp (the C++ library every full C ABI backend shares) through the native
// library's C entry points. @C@ is the prefix of the entry points, @LIB@ the library name.
// (Split in parts: compilers limit string literal sizes.)
namespace rosetta::detail {
inline constexpr const char* pascal_full_runtime_interface[] = {
R"pascal(type
  /// <summary>The kind of an <see cref="ENativeError"/>: the C++ exception it stands for.</summary>
  TNativeErrorKind = (
    /// <summary>Another C++ exception.</summary>
    Native,
    /// <summary>C++ <c>std::invalid_argument</c>.</summary>
    InvalidArgument,
    /// <summary>C++ <c>std::out_of_range</c>.</summary>
    OutOfRange,
    /// <summary>C++ <c>std::domain_error</c>.</summary>
    DomainError,
    /// <summary>C++ <c>std::length_error</c>.</summary>
    LengthError,
    /// <summary>C++ <c>std::overflow_error</c>, <c>std::underflow_error</c> or <c>std::range_error</c>.</summary>
    ArithmeticError,
    /// <summary>C++ <c>std::bad_alloc</c>, or no memory for a call.</summary>
    Allocation,
    /// <summary>A value C++ does not accept, a closed object, or a binding error.</summary>
    Binding,
    /// <summary>A Pascal callback failed without a Pascal exception to raise again.</summary>
    Callback);

  /// <summary>A C++ exception (or a value C++ does not accept) raised in Pascal.</summary>
  ENativeError = class(Exception)
  private
    FKind: TNativeErrorKind;
  public
    /// <summary>An error of kind <c>AKind</c>.</summary>
    constructor Create(AKind: TNativeErrorKind; const AMessage: String);
    /// <summary>The C++ exception it stands for.</summary>
    property Kind: TNativeErrorKind read FKind;
  end;

  /// <summary>A C++ <c>std::complex</c> value.</summary>
  TNativeComplex = record
    Re, Im: Double;
  end;

  /// <summary>A C++ value with no Pascal type, kept as Rosetta's encoding: pass it back to C++ as it is.</summary>
  TNativeValue = record
    Data: TBytes;
  end;

  TNativeObject = class;
  /// <summary>A class of C++ objects.</summary>
  TNativeObjectClass = class of TNativeObject;

  /// <summary>The base of the classes over C++ objects.</summary>
  /// <remarks>
  /// <para>Free the objects you create and the objects functions return by value; an object a C++
  /// function returns by reference or pointer into another object belongs to that object, and is
  /// freed with it. Freeing an object that owns its C++ object (one you created, or a value) deletes the
  /// C++ object; <see cref="Close"/> deletes it at once and leaves the Pascal object closed.</para>
  /// <para>Objects of your Pascal subclasses come back from C++ as themselves.</para>
  /// </remarks>
  TNativeObject = class
  private
    RosettaHolder: QWord;
    RosettaClassIndex: LongWord;
    RosettaId: QWord;
    RosettaDirector: Boolean;
    RosettaShared: Boolean;
    RosettaKey: Integer;
    RosettaOwner: TNativeObject;
    RosettaOwned: array of TNativeObject;
    procedure RosettaAdopt(AObject: TNativeObject; AKey: Integer);
    function RosettaFind(AKey: Integer): TNativeObject;
  public
    /// <summary>A Pascal object with no C++ object yet (the generated constructors make it).</summary>
    constructor RosettaCreate;
    /// <summary>Wraps a C++ object (the runtime calls it).</summary>
    constructor RosettaWrap(AHolder: QWord; AClassIndex: LongWord); virtual;
    /// <summary>Releases the C++ object (deleting it if this object owns it) and the objects this one owns.</summary>
    destructor Destroy; override;
    /// <summary>Deletes the C++ object now if this object owns it; later calls raise <see cref="ENativeError"/>.</summary>
    procedure Close;
    /// <summary>Whether <see cref="Close"/> was called, or C++ took over the object.</summary>
    function IsClosed: Boolean;
    /// <summary>This object as <c>AClass</c> (a C++ base or derived class), or <c>nil</c>.</summary>
    /// <remarks><para>The result belongs to this object: do not free it.</para></remarks>
    function Cast(AClass: TNativeObjectClass): TNativeObject;
    /// <summary>The class name, and whether the object is closed.</summary>
    function ToString: AnsiString; override;
  end;

  /// <summary>The base of C++ <c>std::function</c> values: each has an <c>Invoke</c> method.</summary>
  TNativeFunction = class(TNativeObject)
  end;

)pascal"
};
inline constexpr const char* pascal_full_runtime_implementation[] = {
R"pascal(// ---------------------------------------------------------------- the native library
type
  TRosettaInvokeFn = function(Kind: LongInt; Id: QWord; Generic: LongWord; Data: PByte; Size: PtrUInt; Output: PPByte;
    OutputSize: PPtrUInt): LongInt; cdecl;
  TRosettaRetainFn = procedure(Id: QWord; Delta: LongInt); cdecl;

function RosettaNativeCall(Index: LongWord; Data: PByte; Size: PtrUInt; Output: PPByte; OutputSize: PPtrUInt): LongInt; cdecl;
  external '@LIB@' name '@C@_call';
procedure RosettaNativeFree(Pointer_: Pointer); cdecl; external '@LIB@' name '@C@_free';
function RosettaNativeAlloc(Size: PtrUInt): Pointer; cdecl; external '@LIB@' name '@C@_alloc';
procedure RosettaNativeRelease(Holder: Pointer; Now_: LongInt); cdecl; external '@LIB@' name '@C@_release';
function RosettaNativeInit(Version: LongInt; Invoke: TRosettaInvokeFn; Retain: TRosettaRetainFn): LongInt; cdecl;
  external '@LIB@' name '@C@_init';

// ---------------------------------------------------------------- values
type
  // A value as C++ encodes it. Tags: 0 nil, 1 true, 19 false, 2 integer, 3 big integer (S),
  // 4 real, 5 string, 6 list (Items; also tuples, pairs and real arrays), 9 map (Items: key,
  // value, ...), 10 a new C++ object (Holder, ClassIndex, Parent), 11 an existing Pascal object
  // (Obj), 12 a C++ function (Holder), 14 symbol, 15 bytes, 18 complex (R, Im).
  TRosettaValue = record
    Tag: Byte;
    I: Int64;
    R, Im: Double;
    S: RawByteString;
    Items: array of TRosettaValue;
    Obj: TObject;
    Holder: QWord;
    ClassIndex: LongWord;
    Parent: TNativeObject;
  end;
  TRosettaValues = array of TRosettaValue;
  TRosettaObjects = array of TNativeObject;
  TRosettaCallback = class;

  TRosettaClassInfo = record
    Name: String;
    Bases: array of Integer;
    Cls: TNativeObjectClass;
  end;

  // Encodes values for C++.
  TRosettaWriter = record
    Data: TBytes;
    Len: Integer;
    Callbacks: array of QWord;   // Pascal routines registered for the call
    procedure Reserve(N: Integer);
    procedure U8(V: Byte);
    procedure U32(V: LongWord);
    procedure U64(V: QWord);
    procedure F64(V: Double);
    procedure Text(Tag: Byte; const S: RawByteString);
    procedure Nil_;
    procedure Bool(V: Boolean);
    procedure Int(V: Int64);
    procedure UInt(V: QWord);
    procedure Real(V: Double);
    procedure Str(const V: RawByteString);
    procedure Char_(V: AnsiChar);
    procedure Complex(const V: TNativeComplex);
    procedure Value(const V: TNativeValue);
    procedure Tree(const V: TRosettaValue);
    procedure List(N: Integer);
    procedure Map(N: Integer);
    procedure Obj(V: TNativeObject);
    procedure Holder(H: QWord);
    procedure Callback(V: TRosettaCallback);
  end;

  // A Pascal routine passed to C++ (generated subclasses call the routine).
  TRosettaCallback = class
  public
    Refs: Integer;
    procedure Call(const Args: TRosettaValues; var W: TRosettaWriter); virtual; abstract;
  end;

  // The outcome of a call: its result, and the values C++ wrote back to var parameters.
  TRosettaReply = record
    Value: TRosettaValue;
    Slots: array of LongWord;
    Values: TRosettaValues;
    function Find(Slot: LongWord; out V: TRosettaValue): Boolean;
  end;

  // One call into the native library: Init, the arguments (Arg or ObjArg, then the value), Run.
  TRosettaCall = record
    Index: LongWord;
    W: TRosettaWriter;
    Count: LongWord;
    Objects: TRosettaObjects;
    procedure Init(AIndex: LongWord);
    procedure Arg;
    procedure Slot(N: LongWord);
    procedure ObjArg(V: TNativeObject);
    procedure Run(out Reply: TRosettaReply);
  end;

  // Objects by id (open addressing; ids are never 0).
  TRosettaMap = record
    Keys: array of QWord;
    Values: array of TObject;
    Count: Integer;
    function Slot(Key: QWord): Integer;
    procedure Grow;
    procedure Add(Key: QWord; Value: TObject);
    function TryGetValue(Key: QWord; out Value: TObject): Boolean;
    procedure Remove(Key: QWord);
  end;

var
  RosettaLock: TRTLCriticalSection;
  RosettaNext: QWord = 0;
  RosettaTable: TRosettaMap;    // what C++ refers to by id
  RosettaCauses: TRosettaMap;   // exceptions of Pascal callbacks
  RosettaClasses: array of TRosettaClassInfo;

const
  RosettaCauseMarker = ' [pascal callback #';
  RosettaFunctionClass = High(LongWord);

function RosettaDispatch(Obj: TNativeObject; Generic: LongWord; const Args: TRosettaValues; var W: TRosettaWriter): Boolean; forward;

function TRosettaMap.Slot(Key: QWord): Integer;
var
  Mask: Integer;
begin
  Mask := System.Length(Keys) - 1;
  Result := Integer((Key * QWord(11400714819323198485)) shr 40) and Mask;
  while (Keys[Result] <> 0) and (Keys[Result] <> Key) do Result := (Result + 1) and Mask;
end;

procedure TRosettaMap.Grow;
var
  OldKeys: array of QWord;
  OldValues: array of TObject;
  K, I: Integer;
begin
  OldKeys := Keys;
  OldValues := Values;
  K := System.Length(OldKeys) * 2;
  if K < 64 then K := 64;
  Keys := nil;
  Values := nil;
  SetLength(Keys, K);
  SetLength(Values, K);
  for K := 0 to High(OldKeys) do
    if OldKeys[K] <> 0 then
    begin
      I := Slot(OldKeys[K]);
      Keys[I] := OldKeys[K];
      Values[I] := OldValues[K];
    end;
end;

procedure TRosettaMap.Add(Key: QWord; Value: TObject);
var
  I: Integer;
begin
  if (Count + 1) * 2 > System.Length(Keys) then Grow;
  I := Slot(Key);
  if Keys[I] = 0 then Inc(Count);
  Keys[I] := Key;
  Values[I] := Value;
end;

function TRosettaMap.TryGetValue(Key: QWord; out Value: TObject): Boolean;
var
  I: Integer;
begin
  Value := nil;
  if Count = 0 then Exit(False);
  I := Slot(Key);
  Result := Keys[I] = Key;
  if Result then Value := Values[I];
end;

procedure TRosettaMap.Remove(Key: QWord);
var
  I, J, Mask: Integer;
  K: QWord;
  V: TObject;
begin
  if Count = 0 then Exit;
  I := Slot(Key);
  if Keys[I] <> Key then Exit;
  Keys[I] := 0;
  Values[I] := nil;
  Dec(Count);
  // Re-insert the run after the hole.
  Mask := System.Length(Keys) - 1;
  J := (I + 1) and Mask;
  while Keys[J] <> 0 do
  begin
    K := Keys[J];
    V := Values[J];
    Keys[J] := 0;
    Values[J] := nil;
    Dec(Count);
    Add(K, V);
    J := (J + 1) and Mask;
  end;
end;

procedure RosettaFail(AKind: TNativeErrorKind; const AMessage: String);
begin
  raise ENativeError.Create(AKind, AMessage);
end;

constructor ENativeError.Create(AKind: TNativeErrorKind; const AMessage: String);
begin
  inherited Create(AMessage);
  FKind := AKind;
end;

function RosettaKindName(const V: TRosettaValue): String;
begin
  case V.Tag of
    0: Result := 'nil';
    1, 19: Result := 'a Boolean';
    2, 3: Result := 'an integer';
    4: Result := 'a real';
    5: Result := 'a string';
    6: Result := 'an array';
    9: Result := 'a map';
    10, 11: Result := 'an object';
    12: Result := 'a function';
    14: Result := 'a symbol';
    15: Result := 'bytes';
    18: Result := 'a complex';
  else
    Result := 'a value';
  end;
end;

procedure RosettaMismatch(const V: TRosettaValue; const Wanted: String);
begin
  RosettaFail(TNativeErrorKind.Binding, 'cannot convert ' + RosettaKindName(V) + ' to ' + Wanted);
end;

// ---------------------------------------------------------------- the id table
function RosettaNewId: QWord;
begin
  Inc(RosettaNext);
  Result := RosettaNext;
end;

function RosettaIdOf(O: TNativeObject): QWord;
begin
  EnterCriticalSection(RosettaLock);
  try
    if O.RosettaId = 0 then
    begin
      O.RosettaId := RosettaNewId;
      RosettaTable.Add(O.RosettaId, O);
    end;
    Result := O.RosettaId;
  finally
    LeaveCriticalSection(RosettaLock);
  end;
end;

function RosettaLookup(Id: QWord): TObject;
begin
  EnterCriticalSection(RosettaLock);
  try
    if not RosettaTable.TryGetValue(Id, Result) then Result := nil;
  finally
    LeaveCriticalSection(RosettaLock);
  end;
end;

function RosettaRegister(C: TRosettaCallback): QWord;
begin
  EnterCriticalSection(RosettaLock);
  try
    Result := RosettaNewId;
    C.Refs := 1;
    RosettaTable.Add(Result, C);
  finally
    LeaveCriticalSection(RosettaLock);
  end;
end;

// C++ keeps (Delta > 0) or drops a Pascal routine; Pascal objects live until freed.
procedure RosettaAdjust(Id: QWord; Delta: LongInt);
var
  O: TObject;
  Dropped: TObject = nil;
begin
  EnterCriticalSection(RosettaLock);
  try
    if RosettaTable.TryGetValue(Id, O) and (O is TRosettaCallback) then
    begin
      Inc(TRosettaCallback(O).Refs, Delta);
      if TRosettaCallback(O).Refs <= 0 then
      begin
        RosettaTable.Remove(Id);
        Dropped := O;
      end;
    end;
  finally
    LeaveCriticalSection(RosettaLock);
  end;
  Dropped.Free;
end;

// An exception of a Pascal callback: C++ carries its message back, and the call that reached C++
// raises the original exception.
function RosettaSaveCause(E: TObject): RawByteString;
var
  Id: QWord;
  Message: String;
begin
  if E is Exception then Message := Exception(E).Message else Message := E.ClassName;
  EnterCriticalSection(RosettaLock);
  try
    Id := RosettaNewId;
    RosettaCauses.Add(Id, E);
  finally
    LeaveCriticalSection(RosettaLock);
  end;
  Result := Message + RosettaCauseMarker + IntToStr(Id) + ']';
end;

procedure RosettaRaiseCause(const Message: String);
var
  At, Stop: Integer;
  Id: QWord;
  Cause: TObject = nil;
begin
  At := 0;
  Stop := System.Length(Message);
  if (Stop > 0) and (Message[Stop] = ']') then
  begin
    At := Stop - System.Length(RosettaCauseMarker);
    while (At > 0) and (Copy(Message, At, System.Length(RosettaCauseMarker)) <> RosettaCauseMarker) do Dec(At);
    if (At > 0) and TryStrToQWord(Copy(Message, At + System.Length(RosettaCauseMarker), Stop - At - System.Length(RosettaCauseMarker)), Id) then
    begin
      EnterCriticalSection(RosettaLock);
      try
        if RosettaCauses.TryGetValue(Id, Cause) then RosettaCauses.Remove(Id);
      finally
        LeaveCriticalSection(RosettaLock);
      end;
      if Cause <> nil then raise Cause;
      RosettaFail(TNativeErrorKind.Callback, Copy(Message, 1, At - 1));
    end;
  end;
  RosettaFail(TNativeErrorKind.Callback, Message);
end;

// ---------------------------------------------------------------- classes
function RosettaDerives(From, To_: LongWord): Boolean;
var
  B: Integer;
begin
  if From = To_ then Exit(True);
  if From >= LongWord(System.Length(RosettaClasses)) then Exit(False);
  for B in RosettaClasses[From].Bases do
    if RosettaDerives(LongWord(B), To_) then Exit(True);
  Result := False;
end;

// The class table index of a Pascal class (its nearest generated ancestor's), else -1.
function RosettaIndexOf(C: TClass): Integer;
var
  I: Integer;
begin
  while C <> nil do
  begin
    for I := 0 to High(RosettaClasses) do
      if RosettaClasses[I].Cls = C then Exit(I);
    C := C.ClassParent;
  end;
  Result := -1;
end;

constructor TNativeObject.RosettaCreate;
begin
  inherited Create;
end;

constructor TNativeObject.RosettaWrap(AHolder: QWord; AClassIndex: LongWord);
begin
  inherited Create;
  RosettaHolder := AHolder;
  RosettaClassIndex := AClassIndex;
end;

procedure TNativeObject.RosettaAdopt(AObject: TNativeObject; AKey: Integer);
begin
  AObject.RosettaOwner := Self;
  AObject.RosettaKey := AKey;
  SetLength(RosettaOwned, System.Length(RosettaOwned) + 1);
  RosettaOwned[High(RosettaOwned)] := AObject;
end;

function TNativeObject.RosettaFind(AKey: Integer): TNativeObject;
var
  O: TNativeObject;
begin
  for O in RosettaOwned do
    if O.RosettaKey = AKey then Exit(O);
  Result := nil;
end;

destructor TNativeObject.Destroy;
var
  I: Integer;
  Owned: array of TNativeObject;
begin
  if RosettaId <> 0 then
  begin
    EnterCriticalSection(RosettaLock);
    try
      RosettaTable.Remove(RosettaId);
    finally
      LeaveCriticalSection(RosettaLock);
    end;
  end;
  // Owned objects go first: they may refer into this object.
  Owned := RosettaOwned;
  RosettaOwned := nil;
  for I := High(Owned) downto 0 do
  begin
    Owned[I].RosettaOwner := nil;
    Owned[I].Free;
  end;
  if (RosettaHolder <> 0) and not RosettaShared then RosettaNativeRelease(Pointer(PtrUInt(RosettaHolder)), 0);
  RosettaHolder := 0;
  if RosettaOwner <> nil then
    for I := 0 to High(RosettaOwner.RosettaOwned) do
      if RosettaOwner.RosettaOwned[I] = Self then
      begin
        Delete(RosettaOwner.RosettaOwned, I, 1);
        Break;
      end;
  inherited Destroy;
end;

)pascal",
R"pascal(procedure TNativeObject.Close;
var
  O: TNativeObject;
begin
  if (RosettaHolder <> 0) and not RosettaShared then RosettaNativeRelease(Pointer(PtrUInt(RosettaHolder)), 1);
  RosettaHolder := 0;
  for O in RosettaOwned do
    if O.RosettaShared then O.RosettaHolder := 0;
end;

function TNativeObject.IsClosed: Boolean;
begin
  Result := RosettaHolder = 0;
end;

function TNativeObject.Cast(AClass: TNativeObjectClass): TNativeObject;
var
  Index: Integer;
begin
  if Self is AClass then Exit(Self);
  Index := RosettaIndexOf(AClass);
  if (Index < 0) or (RosettaHolder = 0) or not RosettaDerives(RosettaClassIndex, LongWord(Index)) then Exit(nil);
  Result := RosettaFind(-1 - Index);
  if Result <> nil then Exit;
  Result := AClass.RosettaWrap(RosettaHolder, RosettaClassIndex);
  Result.RosettaShared := True;
  RosettaAdopt(Result, -1 - Index);
end;

function TNativeObject.ToString: AnsiString;
begin
  Result := ClassName;
  if RosettaHolder = 0 then Result := Result + ' (closed)';
end;

// ---------------------------------------------------------------- encoding (Pascal -> C++)
procedure TRosettaWriter.Reserve(N: Integer);
var
  Size: Integer;
begin
  if Len + N <= System.Length(Data) then Exit;
  Size := System.Length(Data) * 2;
  if Size < 64 then Size := 64;
  while Size < Len + N do Size := Size * 2;
  SetLength(Data, Size);
end;

procedure TRosettaWriter.U8(V: Byte);
begin
  Reserve(1);
  Data[Len] := V;
  Inc(Len);
end;

procedure TRosettaWriter.U32(V: LongWord);
begin
  Reserve(4);
  Move(V, Data[Len], 4);
  Inc(Len, 4);
end;

procedure TRosettaWriter.U64(V: QWord);
begin
  Reserve(8);
  Move(V, Data[Len], 8);
  Inc(Len, 8);
end;

procedure TRosettaWriter.F64(V: Double);
begin
  Reserve(8);
  Move(V, Data[Len], 8);
  Inc(Len, 8);
end;

procedure TRosettaWriter.Text(Tag: Byte; const S: RawByteString);
begin
  U8(Tag);
  U64(System.Length(S));
  Reserve(System.Length(S));
  if S <> '' then Move(S[1], Data[Len], System.Length(S));
  Inc(Len, System.Length(S));
end;

procedure TRosettaWriter.Nil_;
begin
  U8(0);
end;

procedure TRosettaWriter.Bool(V: Boolean);
begin
  if V then U8(1) else U8(19);
end;

procedure TRosettaWriter.Int(V: Int64);
begin
  U8(2);
  U64(QWord(V));
end;

procedure TRosettaWriter.UInt(V: QWord);
begin
  if V > QWord(High(Int64)) then Text(3, IntToStr(V)) else Int(Int64(V));
end;

procedure TRosettaWriter.Real(V: Double);
begin
  U8(4);
  F64(V);
end;

procedure TRosettaWriter.Str(const V: RawByteString);
begin
  Text(5, V);
end;

procedure TRosettaWriter.Char_(V: AnsiChar);
begin
  Text(5, V);
end;

procedure TRosettaWriter.Complex(const V: TNativeComplex);
begin
  U8(18);
  F64(V.Re);
  F64(V.Im);
end;

procedure TRosettaWriter.Value(const V: TNativeValue);
begin
  if System.Length(V.Data) = 0 then
  begin
    Nil_;
    Exit;
  end;
  Reserve(System.Length(V.Data));
  Move(V.Data[0], Data[Len], System.Length(V.Data));
  Inc(Len, System.Length(V.Data));
end;

procedure TRosettaWriter.Tree(const V: TRosettaValue);
var
  Item: TRosettaValue;
begin
  case V.Tag of
    0, 1, 19: U8(V.Tag);
    2: Int(V.I);
    3, 5, 14, 15: Text(V.Tag, V.S);
    4: Real(V.R);
    18:
      begin
        U8(18);
        F64(V.R);
        F64(V.Im);
      end;
    6, 9:
      begin
        if V.Tag = 6 then List(System.Length(V.Items)) else Map(System.Length(V.Items) div 2);
        for Item in V.Items do Tree(Item);
      end;
    10, 12: Holder(V.Holder);
    11: if V.Obj is TNativeObject then Obj(TNativeObject(V.Obj)) else Nil_;
  else
    Nil_;
  end;
end;

procedure TRosettaWriter.List(N: Integer);
begin
  U8(6);
  U64(N);
end;

procedure TRosettaWriter.Map(N: Integer);
begin
  U8(9);
  U64(N);
end;

procedure TRosettaWriter.Obj(V: TNativeObject);
begin
  if V = nil then
  begin
    Nil_;
    Exit;
  end;
  U8(10);
  U64(V.RosettaHolder);
  if V.RosettaHolder = 0 then U64(0) else U64(RosettaIdOf(V));
end;

procedure TRosettaWriter.Holder(H: QWord);
begin
  U8(10);
  U64(H);
  U64(0);
end;

procedure TRosettaWriter.Callback(V: TRosettaCallback);
var
  Id: QWord;
begin
  Id := RosettaRegister(V);
  SetLength(Callbacks, System.Length(Callbacks) + 1);
  Callbacks[High(Callbacks)] := Id;
  U8(12);
  U64(Id);
end;

// ---------------------------------------------------------------- decoding (C++ -> Pascal)
type
  TRosettaReader = record
    Data: PByte;
    Size, At: PtrUInt;
    Objects: TRosettaObjects;
    procedure Need(N: QWord);
    function U8: Byte;
    function U32: LongWord;
    function U64: QWord;
    function F64: Double;
    function Bytes: RawByteString;
    function Value: TRosettaValue;
  end;

procedure TRosettaReader.Need(N: QWord);
begin
  if N > Size - At then RosettaFail(TNativeErrorKind.Binding, 'malformed native value');
end;

function TRosettaReader.U8: Byte;
begin
  Need(1);
  Result := Data[At];
  Inc(At);
end;

function TRosettaReader.U32: LongWord;
begin
  Need(4);
  Move(Data[At], Result, 4);
  Inc(At, 4);
end;

function TRosettaReader.U64: QWord;
begin
  Need(8);
  Move(Data[At], Result, 8);
  Inc(At, 8);
end;

function TRosettaReader.F64: Double;
begin
  Need(8);
  Move(Data[At], Result, 8);
  Inc(At, 8);
end;

function TRosettaReader.Bytes: RawByteString;
var
  N: QWord;
begin
  N := U64;
  Need(N);
  SetLength(Result, N);
  if N > 0 then Move(Data[At], Result[1], N);
  Inc(At, N);
end;

function TRosettaReader.Value: TRosettaValue;
var
  N, K: QWord;
  Parent: Int64;
  Index: LongWord;
begin
  Result := Default(TRosettaValue);
  Result.Tag := U8;
  case Result.Tag of
    0, 1, 19: ;
    2: Result.I := Int64(U64);
    3, 5, 14, 15: Result.S := Bytes;
    4: Result.R := F64;
    18:
      begin
        Result.R := F64;
        Result.Im := F64;
      end;
    6, 16:
      begin
        Result.Tag := 6;
        N := U64;
        Need(N);
        SetLength(Result.Items, N);
        for K := 1 to N do Result.Items[K - 1] := Value();
      end;
    7:
      begin
        Result.Tag := 6;
        N := U64;
        Need(N * 8);
        SetLength(Result.Items, N);
        for K := 1 to N do
        begin
          Result.Items[K - 1].Tag := 4;
          Result.Items[K - 1].R := F64;
        end;
      end;
    8:
      begin
        Result.Tag := 6;
        SetLength(Result.Items, 2);
        Result.Items[0] := Value();
        Result.Items[1] := Value();
      end;
    9:
      begin
        N := U64;
        Need(N);
        SetLength(Result.Items, N * 2);
        for K := 1 to N * 2 do Result.Items[K - 1] := Value();
      end;
    10:
      begin
        Result.Holder := U64;
        Result.ClassIndex := U32;
        Parent := Int64(U64);
        if (Parent >= 0) and (Parent < System.Length(Objects)) then Result.Parent := Objects[Parent];
      end;
    11:
      begin
        Result.Obj := RosettaLookup(U64);
        if Result.Obj = nil then RosettaFail(TNativeErrorKind.Binding, 'the Pascal object of a native reference is gone');
      end;
    12: Result.Holder := U64;
    17:
      begin
        Index := U32;
        Result.Tag := 0;
        if (Index < LongWord(System.Length(Objects))) and (Objects[Index] <> nil) then
        begin
          Result.Tag := 11;
          Result.Obj := Objects[Index];
        end;
      end;
  else
    RosettaFail(TNativeErrorKind.Binding, 'malformed native value (tag ' + IntToStr(Result.Tag) + ')');
  end;
end;

// ---------------------------------------------------------------- conversions (C++ -> Pascal)
function RosettaToBool(const V: TRosettaValue): Boolean;
begin
  case V.Tag of
    1: Result := True;
    0, 19: Result := False;
    2: Result := V.I <> 0;
  else
    RosettaMismatch(V, 'Boolean');
    Result := False;
  end;
end;

function RosettaToInt(const V: TRosettaValue; Min, Max: Int64; const Name: String): Int64;
begin
  case V.Tag of
    2: Result := V.I;
    3: if not TryStrToInt64(V.S, Result) then RosettaFail(TNativeErrorKind.Binding, 'integer ' + V.S + ' is out of range for ' + Name);
    4:
      begin
        if (Frac(V.R) <> 0) or (Abs(V.R) > 9.2e18) then RosettaMismatch(V, Name);
        Result := Trunc(V.R);
      end;
    1: Result := 1;
    19: Result := 0;
  else
    RosettaMismatch(V, Name);
    Result := 0;
  end;
  if (Result < Min) or (Result > Max) then RosettaFail(TNativeErrorKind.Binding, 'integer ' + IntToStr(Result) + ' is out of range for ' + Name);
end;

function RosettaToUInt64(const V: TRosettaValue): QWord;
begin
  if V.Tag = 3 then
  begin
    if not TryStrToQWord(V.S, Result) then RosettaFail(TNativeErrorKind.Binding, 'integer ' + V.S + ' is out of range for UInt64');
  end
  else
    Result := QWord(RosettaToInt(V, 0, High(Int64), 'UInt64'));
end;

function RosettaToInt8(const V: TRosettaValue): Int8; begin Result := RosettaToInt(V, Low(Int8), High(Int8), 'Int8'); end;
function RosettaToInt16(const V: TRosettaValue): Int16; begin Result := RosettaToInt(V, Low(Int16), High(Int16), 'Int16'); end;
function RosettaToInt32(const V: TRosettaValue): Int32; begin Result := RosettaToInt(V, Low(Int32), High(Int32), 'Int32'); end;
function RosettaToInt64(const V: TRosettaValue): Int64; begin Result := RosettaToInt(V, Low(Int64), High(Int64), 'Int64'); end;
function RosettaToUInt8(const V: TRosettaValue): UInt8; begin Result := RosettaToInt(V, 0, High(UInt8), 'UInt8'); end;
function RosettaToUInt16(const V: TRosettaValue): UInt16; begin Result := RosettaToInt(V, 0, High(UInt16), 'UInt16'); end;
function RosettaToUInt32(const V: TRosettaValue): UInt32; begin Result := RosettaToInt(V, 0, High(UInt32), 'UInt32'); end;

function RosettaToDouble(const V: TRosettaValue): Double;
begin
  case V.Tag of
    4: Result := V.R;
    2: Result := V.I;
    3: Result := StrToFloat(V.S);
  else
    RosettaMismatch(V, 'Double');
    Result := 0;
  end;
end;

function RosettaToSingle(const V: TRosettaValue): Single;
begin
  Result := RosettaToDouble(V);
end;

function RosettaToComplex(const V: TRosettaValue): TNativeComplex;
begin
  if V.Tag = 18 then
  begin
    Result.Re := V.R;
    Result.Im := V.Im;
  end
  else
  begin
    Result.Re := RosettaToDouble(V);
    Result.Im := 0;
  end;
end;

function RosettaToString(const V: TRosettaValue): String;
begin
  Result := '';
  case V.Tag of
    5, 14, 15: Result := V.S;
    0: Result := '';
  else
    RosettaMismatch(V, 'String');
  end;
end;

function RosettaToChar(const V: TRosettaValue): AnsiChar;
begin
  case V.Tag of
    5, 15: if System.Length(V.S) = 1 then Exit(V.S[1]);
    2: if (V.I >= 0) and (V.I <= 255) then Exit(AnsiChar(V.I));
  end;
  RosettaMismatch(V, 'Char');
  Result := #0;
end;

function RosettaToValue(const V: TRosettaValue): TNativeValue;
var
  W: TRosettaWriter;
begin
  W := Default(TRosettaWriter);
  W.Tree(V);
  Result.Data := Copy(W.Data, 0, W.Len);
end;

// The elements of an array (nil is empty).
procedure RosettaItems(const V: TRosettaValue; const Wanted: String);
begin
  if not (V.Tag in [0, 6]) then RosettaMismatch(V, Wanted);
end;

// The keys and values of a map (or a list of pairs), alternating.
function RosettaPairs(const V: TRosettaValue): TRosettaValues;
var
  K: Integer;
begin
  case V.Tag of
    0: Result := nil;
    9: Result := V.Items;
    6:
      begin
        SetLength(Result, System.Length(V.Items) * 2);
        for K := 0 to High(V.Items) do
        begin
          if (V.Items[K].Tag <> 6) or (System.Length(V.Items[K].Items) <> 2) then RosettaMismatch(V, 'a map');
          Result[2 * K] := V.Items[K].Items[0];
          Result[2 * K + 1] := V.Items[K].Items[1];
        end;
      end;
  else
    RosettaMismatch(V, 'a map');
  end;
end;

// The items of a tuple of N.
procedure RosettaTuple(const V: TRosettaValue; N: Integer);
begin
  if (V.Tag <> 6) or (System.Length(V.Items) <> N) then RosettaMismatch(V, 'a tuple of ' + IntToStr(N));
end;

procedure RosettaArity(const Args: TRosettaValues; N: Integer);
begin
  if System.Length(Args) <> N then
    RosettaFail(TNativeErrorKind.Binding, 'Pascal routine takes ' + IntToStr(N) + ' arguments, not ' + IntToStr(System.Length(Args)));
end;

)pascal",
R"pascal(// An object of class AClass (nil for nil): a new Pascal object over a C++ object (owned by the
// argument it refers into, if any), or an existing one.
function RosettaToObject(const V: TRosettaValue; AClass: TNativeObjectClass): TNativeObject;
var
  Cls: TNativeObjectClass;
begin
  case V.Tag of
    0: Exit(nil);
    10:
      begin
        if V.ClassIndex >= LongWord(System.Length(RosettaClasses)) then
        begin
          RosettaNativeRelease(Pointer(PtrUInt(V.Holder)), 0);
          RosettaFail(TNativeErrorKind.Binding, 'C++ result type is not exported to Pascal');
        end;
        Cls := RosettaClasses[V.ClassIndex].Cls;
        if not Cls.InheritsFrom(AClass) then Cls := AClass;   // a further base: no Pascal subclass
        Result := Cls.RosettaWrap(V.Holder, V.ClassIndex);
        if V.Parent <> nil then V.Parent.RosettaAdopt(Result, 0);
      end;
    11:
      begin
        if not (V.Obj is TNativeObject) then RosettaMismatch(V, AClass.ClassName);
        Result := TNativeObject(V.Obj).Cast(AClass);
        if Result = nil then RosettaMismatch(V, AClass.ClassName);
      end;
  else
    RosettaMismatch(V, AClass.ClassName);
    Result := nil;
  end;
end;

// A C++ function: an object of AClass (a TNativeFunction).
function RosettaToFunction(const V: TRosettaValue; AClass: TNativeObjectClass): TNativeObject;
begin
  case V.Tag of
    0: Result := nil;
    12: Result := AClass.RosettaWrap(V.Holder, RosettaFunctionClass);
  else
    RosettaMismatch(V, AClass.ClassName);
    Result := nil;
  end;
end;

// ---------------------------------------------------------------- calls
function TRosettaReply.Find(Slot: LongWord; out V: TRosettaValue): Boolean;
var
  K: Integer;
begin
  for K := 0 to High(Slots) do
    if Slots[K] = Slot then
    begin
      V := Values[K];
      Exit(True);
    end;
  V := Default(TRosettaValue);
  Result := False;
end;

procedure TRosettaCall.Init(AIndex: LongWord);
begin
  Self := Default(TRosettaCall);
  Index := AIndex;
  W.U32(0);   // the argument count, set by Run
end;

procedure TRosettaCall.Arg;
begin
  Inc(Count);
  SetLength(Objects, System.Length(Objects) + 1);
end;

procedure TRosettaCall.Slot(N: LongWord);
begin
  W.U8(13);
  W.U32(N);
end;

procedure TRosettaCall.ObjArg(V: TNativeObject);
begin
  Arg;
  Objects[High(Objects)] := V;
  W.Obj(V);
end;

procedure RosettaDropCallbacks(var W: TRosettaWriter);
var
  Id: QWord;
begin
  for Id in W.Callbacks do RosettaAdjust(Id, -1);
  W.Callbacks := nil;
end;

// Pascal buffers Output while C++ writes std::cout and stdout directly (and flushes them after each call
// and before each callback): Output is flushed before C++ runs, so piped output stays in order.
procedure RosettaFlushOutput;
begin
  if TextRec(System.Output).Mode = fmOutput then
  begin
    Flush(System.Output);
    InOutRes := 0;
  end;
end;

procedure TRosettaCall.Run(out Reply: TRosettaReply);
var
  Output: PByte = nil;
  OutputSize: PtrUInt = 0;
  Status: LongInt;
  R: TRosettaReader;
  Kind: TRosettaValue;
  Text: TRosettaValue;
  N, K: LongWord;
  Mask: TFPUExceptionMask;
  ErrorKind: TNativeErrorKind;
begin
  Reply := Default(TRosettaReply);
  Move(Count, W.Data[0], 4);
  // C++ computes with IEEE semantics: no Pascal floating point exceptions in native code.
  Mask := GetExceptionMask;
  SetExceptionMask([exInvalidOp, exDenormalized, exZeroDivide, exOverflow, exUnderflow, exPrecision]);
  RosettaFlushOutput;
  try
    Status := RosettaNativeCall(Index, @W.Data[0], W.Len, @Output, @OutputSize);
  finally
    SetExceptionMask(Mask);
  end;
  RosettaDropCallbacks(W);
  R := Default(TRosettaReader);
  R.Data := Output;
  R.Size := OutputSize;
  R.Objects := Objects;
  try
    case Status of
      0:
        begin
          Reply.Value := R.Value;
          N := R.U32;
          SetLength(Reply.Slots, N);
          SetLength(Reply.Values, N);
          for K := 1 to N do
          begin
            Reply.Slots[K - 1] := R.U32;
            Reply.Values[K - 1] := R.Value;
          end;
        end;
      1, 2:
        begin
          Kind := R.Value;
          Text := R.Value;
          if Status = 2 then RosettaRaiseCause(Text.S);
          ErrorKind := TNativeErrorKind.Native;
          if Kind.S = 'binding' then ErrorKind := TNativeErrorKind.Binding
          else if Kind.S = 'invalid-argument' then ErrorKind := TNativeErrorKind.InvalidArgument
          else if Kind.S = 'out-of-range' then ErrorKind := TNativeErrorKind.OutOfRange
          else if Kind.S = 'domain-error' then ErrorKind := TNativeErrorKind.DomainError
          else if Kind.S = 'length-error' then ErrorKind := TNativeErrorKind.LengthError
          else if Kind.S = 'arithmetic-error' then ErrorKind := TNativeErrorKind.ArithmeticError
          else if Kind.S = 'allocation' then ErrorKind := TNativeErrorKind.Allocation;
          RosettaFail(ErrorKind, Text.S);
        end;
    else
      RosettaFail(TNativeErrorKind.Allocation, 'out of memory in a native call');
    end;
  finally
    if Output <> nil then RosettaNativeFree(Output);
  end;
end;

// Starts constructing Obj with C++ constructor number Index; with ADirector, as a C++ subclass
// whose virtual methods call the Pascal methods. The arguments follow, then RosettaFinish.
procedure RosettaConstruct(Obj: TNativeObject; var C: TRosettaCall; Index: LongWord; ADirector: Boolean);
begin
  C.Init(Index);
  C.Arg;
  if ADirector then C.W.Int(Int64(RosettaIdOf(Obj))) else C.W.Int(0);
  Obj.RosettaDirector := ADirector;
end;

procedure RosettaFinish(Obj: TNativeObject; var C: TRosettaCall);
var
  Reply: TRosettaReply;
begin
  C.Run(Reply);
  if Reply.Value.Tag <> 10 then RosettaMismatch(Reply.Value, 'a new object');
  Obj.RosettaHolder := Reply.Value.Holder;
  Obj.RosettaClassIndex := Reply.Value.ClassIndex;
end;

// ---------------------------------------------------------------- calls from C++
function RosettaInvoke(Kind: LongInt; Id: QWord; Generic: LongWord; Data: PByte; Size: PtrUInt; Output: PPByte;
  OutputSize: PPtrUInt): LongInt; cdecl;
var
  R: TRosettaReader;
  W: TRosettaWriter;
  Args: TRosettaValues;
  N, K: LongWord;
  Target: TObject;
  Buffer: PByte;
begin
  Result := 0;
  W := Default(TRosettaWriter);
  try
    R := Default(TRosettaReader);
    R.Data := Data;
    R.Size := Size;
    N := R.U32;
    SetLength(Args, N);
    for K := 1 to N do Args[K - 1] := R.Value;
    if Kind = 0 then
    begin
      Target := RosettaLookup(Id);
      if not (Target is TRosettaCallback) then RosettaFail(TNativeErrorKind.Binding, 'the Pascal routine of a native reference is gone');
      TRosettaCallback(Target).Call(Args, W);
    end
    else
    begin
      if (N = 0) or (Args[0].Tag <> 11) or not (Args[0].Obj is TNativeObject) then
        RosettaFail(TNativeErrorKind.Binding, 'virtual method called without an object');
      if not RosettaDispatch(TNativeObject(Args[0].Obj), Generic, Copy(Args, 1, N - 1), W) then
        RosettaFail(TNativeErrorKind.Binding, 'no Pascal method for C++ virtual method ' + IntToStr(Generic));
    end;
    // Pascal routines inside the result stay registered until C++ keeps or drops them.
    W.Callbacks := nil;
    RosettaFlushOutput;
  except
    W := Default(TRosettaWriter);
    W.Str(RosettaSaveCause(TObject(AcquireExceptionObject)));
    Result := 2;
  end;
  Buffer := RosettaNativeAlloc(W.Len + 1);
  if Buffer = nil then
  begin
    Output^ := nil;
    OutputSize^ := 0;
    Exit(3);
  end;
  if W.Len > 0 then Move(W.Data[0], Buffer^, W.Len);
  Output^ := Buffer;
  OutputSize^ := W.Len;
end;

procedure RosettaRetain(Id: QWord; Delta: LongInt); cdecl;
begin
  RosettaAdjust(Id, Delta);
end;

procedure RosettaStart;
begin
  InitCriticalSection(RosettaLock);
  if RosettaNativeInit(1, @RosettaInvoke, @RosettaRetain) <> 0 then
    raise ENativeError.Create(TNativeErrorKind.Binding, 'native library @LIB@ was generated by a different Rosetta version');
end;

)pascal"
};
} // namespace rosetta::detail
