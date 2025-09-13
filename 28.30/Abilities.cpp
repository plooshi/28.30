#include "pch.h"
#include "Abilities.h"

void Abilities::InternalServerTryActivateAbility(UFortAbilitySystemComponentAthena* AbilitySystemComponent, FGameplayAbilitySpecHandle Handle, bool InputPressed, FPredictionKey& PredictionKey, FGameplayEventData* TriggerEventData) {
    auto Spec = AbilitySystemComponent->ActivatableAbilities.Items.Search([&](FGameplayAbilitySpec& item) {
        return item.Handle.Handle == Handle.Handle;
        });

    if (!Spec)
        return AbilitySystemComponent->ClientActivateAbilityFailed(Handle, PredictionKey.Current);


    Spec->InputPressed = true;

    UGameplayAbility* InstancedAbility = nullptr;

    auto InternalTryActivateAbility = (bool (*)(UAbilitySystemComponent*, FGameplayAbilitySpecHandle, FPredictionKey, UGameplayAbility**, void*, const FGameplayEventData*)) Sarah::Offsets::InternalTryActivateAbility;
    if (!InternalTryActivateAbility(AbilitySystemComponent, Handle, PredictionKey, &InstancedAbility, nullptr, TriggerEventData))
    {
        AbilitySystemComponent->ClientActivateAbilityFailed(Handle, PredictionKey.Current);
        Spec->InputPressed = false;
        AbilitySystemComponent->ActivatableAbilities.MarkItemDirty(*Spec);
    }
    //printf("aa %s\n", InstancedAbility->GetName().c_str());
}

void Abilities::GiveAbility(UAbilitySystemComponent* AbilitySystemComponent, TSubclassOf<UFortGameplayAbility> Ability)
{
    if (!AbilitySystemComponent || !Ability)
        return;

    FGameplayAbilitySpec Spec{};

    ((void (*)(FGameplayAbilitySpec*, UGameplayAbility*, int, int, UObject*)) (Sarah::Offsets::ImageBase + 0x728D9B4))(&Spec, (UGameplayAbility*)Ability->DefaultObject, 1, -1, nullptr);
    ((FGameplayAbilitySpecHandle * (__fastcall*)(UAbilitySystemComponent*, FGameplayAbilitySpecHandle*, FGameplayAbilitySpec)) (Sarah::Offsets::ImageBase + 0x72492BC))(AbilitySystemComponent, &Spec.Handle, Spec);
}

void Abilities::GiveAbilitySet(UAbilitySystemComponent* AbilitySystemComponent, UFortAbilitySet* Set)
{
    for (auto& Abilite : Set->GameplayAbilities)
    {
        printf("GiveAbilitySet[%s]\n", Abilite->GetName().c_str());
    }

    printf("on %s\n", AbilitySystemComponent->GetOwner()->GetName().c_str());
    /*TScriptInterface<IAbilitySystemInterface> ScriptInterface;
    ScriptInterface.ObjectPointer = AbilitySystemComponent->GetOwner();
    ScriptInterface.InterfacePointer = Utils::GetInterface<IFortAbilitySystemInterface>(ScriptInterface.ObjectPointer);
    UFortKismetLibrary::EquipFortAbilitySet(ScriptInterface, Set, nullptr);*/
    if (Set)
    {
        for (auto& GameplayAbility : Set->GameplayAbilities)
            GiveAbility(AbilitySystemComponent, GameplayAbility);
        for (auto& GameplayEffect : Set->PassiveGameplayEffects)
            AbilitySystemComponent->BP_ApplyGameplayEffectToSelf(GameplayEffect.GameplayEffect.Get(), GameplayEffect.Level, AbilitySystemComponent->MakeEffectContext());
    }
}

void Abilities::Hook() {
    Utils::HookEvery<UAbilitySystemComponent>(Sarah::Offsets::InternalServerTryActivateAbilityVft, InternalServerTryActivateAbility);
}
