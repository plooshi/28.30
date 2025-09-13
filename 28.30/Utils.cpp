#include "pch.h"
#include "Utils.h"

UObject* Utils::FindObject(const wchar_t* ObjectPath, UClass* Class)
{
    auto Object = InternalFindObject(ObjectPath, Class);
    return Object ? Object : InternalLoadObject(ObjectPath, Class);
}

TArray<AActor*> Utils::GetAll(UClass* Class)
{
    TArray<AActor*> ret;
    UGameplayStatics::GetAllActorsOfClass(UWorld::GetWorld(), Class, &ret);
    return ret;
}

// Basic.hpp
FName::FName(FString str) {
    *this = UKismetStringLibrary::Conv_StringToName(str);
}

UObject* SDK::InternalGet(FSoftObjectPtr* Ptr, UClass* Class)
{
    if (!Ptr)
        return nullptr;

    auto Ret = Ptr->WeakPtr.ObjectIndex && Ptr->WeakPtr.ObjectSerialNumber ? Ptr->Get() : nullptr;
    if ((!Ret || !Ret->IsA(Class)) && Ptr->ObjectID.AssetPath.AssetName.ComparisonIndex > 0)
    {
        auto Path = Ptr->ObjectID.AssetPath.PackageName.GetRawWString();
        if (Ptr->ObjectID.AssetPath.AssetName)
        {
            Path += L"." + Ptr->ObjectID.AssetPath.AssetName.GetRawWString();
        }
        Ptr->WeakPtr = Ret = Utils::FindObject(Path.c_str(), Class);
    }
    return Ret;
}


AActor* Utils::SpawnActor(UClass* Class, FTransform& Transform, AActor* Owner)
{
    FActorSpawnParameters addr{};

    addr.Owner = Owner;
    addr.bDeferConstruction = false;
    addr.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    addr.NameMode = ESpawnActorNameMode::Requested;

    return ((AActor * (*)(UWorld*, UClass*, FTransform const*, FActorSpawnParameters*))(Sarah::Offsets::ImageBase + 0x1999190))(UWorld::GetWorld(), Class, &Transform, &addr);
    //return UGameplayStatics::FinishSpawningActor(UGameplayStatics::BeginDeferredActorSpawnFromClass(UWorld::GetWorld(), Class, Transform, ESpawnActorCollisionHandlingMethod::AlwaysSpawn, Owner), Transform);
}

AActor* Utils::SpawnActor(UClass* Class, FVector& Loc, FRotator& Rot, AActor* Owner)
{
    FTransform Transform(Loc, Rot);
    return SpawnActor(Class, Transform, Owner);
}

void FFrame::StepCompiledIn(void* const Result, bool ForceExplicitProp)
{
    if (Code && !ForceExplicitProp)
    {
        ((void (*)(FFrame*, UObject*, void* const)) Sarah::Offsets::Step)(this, Object, Result);
    }
    else
    {
        FField* _Prop = PropertyChainForCompiledIn;
        PropertyChainForCompiledIn = _Prop->Next;
        ((void (*)(FFrame*, void* const, FField*)) Sarah::Offsets::StepExplicitProperty)(this, Result, _Prop);
    }
}


void FFrame::IncrementCode() {
    Code = (uint8_t*)(__int64(Code) + (bool)Code);
}

double FRotator::UnwindDegrees(double Angle) {

    Angle = fmod(Angle, 360.0f); // rat

    if (Angle < 0.)
        Angle += 360.;

    if (Angle > 180.)
        Angle -= 360.;

    return Angle;
}

double FRotator::ClampAxis(double Angle)
{
    Angle = fmod(Angle, 360.); // rat

    if (Angle < 0.)
        Angle += 360.;
    return Angle;
}

FRotator FQuat::Rotator()
{
    const double SingularityTest = Z * X - W * Y;
    const double YawY = 2. * (W * Z + X * Y);
    const double YawX = (1. - 2. * ((Y * Y) + (Z * Z)));

    const double SINGULARITY_THRESHOLD = 0.4999995;
    const double RAD_TO_DEG = 57.29577951308232;
    FRotator RotatorFromQuat{};

    if (SingularityTest < -SINGULARITY_THRESHOLD)
    {
        RotatorFromQuat.pitch = -90.;
        RotatorFromQuat.Yaw = atan2(YawY, YawX) * RAD_TO_DEG;
        RotatorFromQuat.Roll = FRotator::NormalizeAxis(-RotatorFromQuat.Yaw - (2. * atan2(X, W) * RAD_TO_DEG));
    }
    else if (SingularityTest > SINGULARITY_THRESHOLD)
    {
        RotatorFromQuat.pitch = 90.;
        RotatorFromQuat.Yaw = atan2(YawY, YawX) * RAD_TO_DEG;
        RotatorFromQuat.Roll = FRotator::NormalizeAxis(RotatorFromQuat.Yaw - (2. * atan2(X, W) * RAD_TO_DEG));
    }
    else
    {
        RotatorFromQuat.pitch = asin(2. * SingularityTest) * RAD_TO_DEG;
        RotatorFromQuat.Yaw = atan2(YawY, YawX) * RAD_TO_DEG;
        RotatorFromQuat.Roll = atan2(-2. * (W * X + Y * Z), (1. - 2. * ((X * X) + (Y * Y)))) * RAD_TO_DEG;
    }

    return RotatorFromQuat;
}

void UC::_TArrayAdd(void*& Data, int32& NumElements, int32& MaxElements, int32 _Elem_Sz, const void* Element) {
    if (NumElements + 1 > MaxElements) Data = FMemory::Realloc(Data, (MaxElements = NumElements + 1) * _Elem_Sz);

    __movsb(PBYTE(Data) + (NumElements * _Elem_Sz), (const PBYTE)Element, _Elem_Sz);
    NumElements++;
}

FVector_NetQuantize10 FVector::Quantize10()
{
    FVector_NetQuantize10 ret;
    ret.X = Utils::precision(X, 1);
    ret.Y = Utils::precision(Y, 1);
    ret.Z = Utils::precision(Z, 1);
    return ret;
}

FVector_NetQuantize100 FVector::Quantize100()
{
    FVector_NetQuantize100 ret;
    ret.X = Utils::precision(X, 2);
    ret.Y = Utils::precision(Y, 2);
    ret.Z = Utils::precision(Z, 2);
    return ret;
}

// probably scuffed but wtv!
FVector_NetQuantizeNormal FVector::QuantizeNormal()
{
    FVector_NetQuantizeNormal ret;
    ret.X = X > 0 ? X / 100000 : -(-X / 100000);
    ret.Y = Y > 0 ? Y / 100000 : -(-Y / 100000);
    ret.Z = Z > 0 ? Z / 100000 : -(-Z / 100000);
    return ret;
}

float Utils::EvaluateScalableFloat(FScalableFloat& Float, float Level)
{
    if (!Float.Curve.CurveTable)
        return Float.Value;

    float Out;
    UDataTableFunctionLibrary::EvaluateCurveTableRow(Float.Curve.CurveTable, Float.Curve.RowName, Level, nullptr, &Out, FString());
    return Out;
}